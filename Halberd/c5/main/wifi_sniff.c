#include "wifi_sniff.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "link.h"
#include "link_protocol.h"
#include "wifi.h"

static const char *TAG = "wifi_sniff";

#define SNIFF_REQ_QUEUE_LEN          4
#define SNIFF_FRAME_QUEUE_LEN       64
#define SNIFF_TASK_STACK          6144
#define SNIFF_TASK_PRIORITY          5

#define SNIFF_MIN_DWELL_MS_PER_CH   80u
#define SNIFF_MAX_DWELL_MS_PER_CH  500u

// Buffered captured frame from the ISR-context promiscuous RX callback.
// Sized to match the wire payload cap so the task can copy directly.
typedef struct {
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  is_response;
    uint8_t  src_mac[6];
    uint8_t  dst_mac[6];
    uint8_t  bssid[6];
    uint16_t payload_len;
    uint8_t  payload[LINK_WIFI_PROBE_PAYLOAD_MAX];
} captured_probe_t;

static QueueHandle_t s_req_queue;
static QueueHandle_t s_frame_queue;
static volatile bool s_sniff_busy;
static volatile bool s_capture_responses;

// Bench-only diagnostic counters. Bumped from the promiscuous RX callback
// (ISR-ish context). Reset at the start of every sniff session; printed in
// the "done" log so we can see what the radio is actually delivering vs.
// what survives our filters. Investigating "stage 8: zero 5 GHz events".
static volatile uint32_t s_diag_cb_total;
static volatile uint32_t s_diag_cb_mgmt;
static volatile uint32_t s_diag_cb_probe;
// Per-band probe-frame counters: 2.4 GHz channels (1-14) and 5 GHz
// channels (36+). Lets us verify the scan-driven sweep actually visits
// 5 GHz, not just 2.4 GHz. Sum should equal s_diag_cb_probe.
static volatile uint32_t s_diag_cb_probe_2g;
static volatile uint32_t s_diag_cb_probe_5g;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Promiscuous RX callback. Runs in ISR-ish context: keep work tight.
// Filters to 802.11 management frames, then to subtype 4 (probe request)
// or 5 (probe response). The rest get dropped.
static void probe_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_sniff_busy || buf == NULL) return;
    s_diag_cb_total++;
    if (type != WIFI_PKT_MGMT) return;
    s_diag_cb_mgmt++;

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    const uint16_t sig_len = pkt->rx_ctrl.sig_len;
    if (sig_len < 24) return;  // shortest mgmt frame header

    // Frame-control byte: type in bits 2-3, subtype in bits 4-7.
    uint8_t fc0 = frame[0];
    uint8_t ftype   = (fc0 >> 2) & 0x03;
    uint8_t fsubtype = (fc0 >> 4) & 0x0F;
    if (ftype != 0) return;
    uint8_t is_resp;
    if (fsubtype == 4) {
        is_resp = 0;
    } else if (fsubtype == 5 && s_capture_responses) {
        is_resp = 1;
    } else {
        return;
    }
    s_diag_cb_probe++;
    if (pkt->rx_ctrl.channel >= 36) s_diag_cb_probe_5g++;
    else                            s_diag_cb_probe_2g++;

    captured_probe_t cap;
    cap.rssi    = pkt->rx_ctrl.rssi;
    cap.channel = pkt->rx_ctrl.channel;
    cap.is_response = is_resp;
    memcpy(cap.dst_mac, frame + 4,  6);   // addr1
    memcpy(cap.src_mac, frame + 10, 6);   // addr2
    memcpy(cap.bssid,   frame + 16, 6);   // addr3

    uint16_t copy = sig_len;
    if (copy > LINK_WIFI_PROBE_PAYLOAD_MAX) copy = LINK_WIFI_PROBE_PAYLOAD_MAX;
    cap.payload_len = copy;
    memcpy(cap.payload, frame, copy);
    // Zero-pad tail so the wire frame is deterministic.
    if (copy < LINK_WIFI_PROBE_PAYLOAD_MAX) {
        memset(cap.payload + copy, 0, LINK_WIFI_PROBE_PAYLOAD_MAX - copy);
    }

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_frame_queue, &cap, &woken);
    if (woken == pdTRUE) portYIELD_FROM_ISR();
}

static void emit_event(const captured_probe_t *cap, uint32_t scan_id) {
    struct link_wifi_probe_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.scan_id     = scan_id;
    ev.rssi        = cap->rssi;
    ev.channel     = cap->channel;
    ev.is_response = cap->is_response;
    memcpy(ev.src_mac, cap->src_mac, 6);
    memcpy(ev.dst_mac, cap->dst_mac, 6);
    memcpy(ev.bssid,   cap->bssid,   6);
    ev.payload_len = cap->payload_len;
    memcpy(ev.payload, cap->payload, LINK_WIFI_PROBE_PAYLOAD_MAX);
    link_send_wifi_probe_event(&ev);
}

// Hop one channel and drain everything the ISR pushes during the dwell.
static void sniff_one_channel(uint8_t channel, uint32_t dwell_ms,
                              uint32_t scan_id, uint16_t *out_count) {
    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        // Regulatory-domain skip is common on 5 GHz with country "01" —
        // log at debug, not warn.
        ESP_LOGD(TAG, "set_channel %u: %s", channel, esp_err_to_name(err));
        return;
    }

    uint32_t deadline = now_ms() + dwell_ms;
    captured_probe_t cap;
    while (now_ms() < deadline) {
        TickType_t wait = pdMS_TO_TICKS(20);
        if (xQueueReceive(s_frame_queue, &cap, wait) != pdTRUE) continue;
        emit_event(&cap, scan_id);
        *out_count += 1;
    }
}

static void wifi_sniff_task(void *arg) {
    (void)arg;
    struct link_wifi_probe_req req;

    while (1) {
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (req.channel_count == 0 || req.channel_count > LINK_WIFI_MAX_CHANNELS) {
            struct link_wifi_probe_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_ERROR;
            link_send_wifi_probe_done(&done);
            continue;
        }

        // Serialise against the wifi scan + ieee802154 scan tasks via the
        // shared radio mutex. Wait briefly rather than reject immediately:
        // the S3's probeDetectionTask fires the IEEE scan and the Wi-Fi
        // sniff at the same millisecond, and ieee_scan_task has the higher
        // priority (6 vs 5), so a 0-timeout grab loses every race. IEEE
        // scans are short (~2.24s for 16 channels at 125ms dwell), so a
        // 5s wait covers the worst case and lets sniff run right after.
        if (xSemaphoreTake(wifi_radio_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            struct link_wifi_probe_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_BUSY;
            link_send_wifi_probe_done(&done);
            ESP_LOGW(TAG, "sniff id=%" PRIu32 " rejected: radio busy",
                     req.scan_id);
            continue;
        }

        s_capture_responses = (req.capture_responses != 0);
        s_diag_cb_total = 0;
        s_diag_cb_mgmt  = 0;
        s_diag_cb_probe = 0;
        s_diag_cb_probe_2g = 0;
        s_diag_cb_probe_5g = 0;
        s_sniff_busy = true;

        // Drain any pre-existing junk in the frame queue before arming.
        captured_probe_t drop;
        while (xQueueReceive(s_frame_queue, &drop, 0) == pdTRUE) { }

        // Bring the Wi-Fi stack up for this session only. Mirrors the
        // S3's radioStartSTA pattern: between sniff sessions the C5 has
        // no Wi-Fi loaded at all.
        esp_err_t up_err = wifi_radio_up();
        if (up_err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_radio_up: %s", esp_err_to_name(up_err));
            s_sniff_busy = false;
            xSemaphoreGive(wifi_radio_mutex);
            struct link_wifi_probe_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_ERROR;
            link_send_wifi_probe_done(&done);
            continue;
        }

        // Disable Wi-Fi power save before tuning. PS_NONE keeps the radio
        // active across the dwell so any captured frames hit our cb
        // immediately rather than landing after a wake transition.
        esp_wifi_set_ps(WIFI_PS_NONE);

        // Filter + callback BEFORE set_promiscuous(true): matches the
        // IDF v6.0.1 official simple_sniffer example. Anything that
        // briefly arrives in the gap between mode-enable and cb-register
        // would otherwise hit a null/default callback and be lost.
        wifi_promiscuous_filter_t flt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
        esp_wifi_set_promiscuous_filter(&flt);
        esp_wifi_set_promiscuous_rx_cb(probe_rx_cb);

        esp_err_t err = esp_wifi_set_promiscuous(true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set_promiscuous(true): %s", esp_err_to_name(err));
            wifi_radio_down();
            s_sniff_busy = false;
            xSemaphoreGive(wifi_radio_mutex);
            struct link_wifi_probe_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_ERROR;
            link_send_wifi_probe_done(&done);
            continue;
        }

        uint32_t start = now_ms();
        uint16_t event_count = 0;

        ESP_LOGI(TAG, "sniff id=%" PRIu32 " scan-driven full-band cap_resp=%u",
                 req.scan_id, (unsigned)req.capture_responses);

        // Scan-driven channel hop: let esp_wifi_scan_start orchestrate
        // the channel sweep instead of our own set_channel loop. The
        // per-channel approach reliably returned cb total=0 on the C5
        // (IDF v6.0.1) even though every setup checkbox passed. The
        // theory: the IDF's internal scan engine activates RX in a way
        // that bare set_channel + dwell doesn't, and promiscuous-mode
        // frames arrive via the cb during the scan's active phase.
        // Mirror of the working stage-4 pattern in wifi.c.
        wifi_scan_config_t scan_cfg = {0};
        scan_cfg.ssid        = NULL;
        scan_cfg.bssid       = NULL;
        scan_cfg.channel     = 0;
        scan_cfg.show_hidden = true;
        scan_cfg.scan_type   = WIFI_SCAN_TYPE_ACTIVE;
        scan_cfg.scan_time.active.min = 250;
        scan_cfg.scan_time.active.max = 300;
        scan_cfg.scan_time.passive    = 300;

        esp_err_t scan_err = esp_wifi_scan_start(&scan_cfg, true);
        if (scan_err != ESP_OK) {
            ESP_LOGW(TAG, "scan_start failed: %s", esp_err_to_name(scan_err));
        }

        // Drain whatever the cb queued during the scan window.
        captured_probe_t cap;
        while (xQueueReceive(s_frame_queue, &cap, 0) == pdTRUE) {
            emit_event(&cap, req.scan_id);
            event_count++;
        }

        esp_wifi_set_promiscuous(false);
        // Tear the Wi-Fi stack down so the next radio (BLE / 802.15.4)
        // takes its turn against a clean slate.
        wifi_radio_down();
        s_sniff_busy = false;

        // One last drain of stragglers so the next sniff starts clean.
        while (xQueueReceive(s_frame_queue, &drop, 0) == pdTRUE) { }

        xSemaphoreGive(wifi_radio_mutex);

        struct link_wifi_probe_done done = {0};
        done.scan_id     = req.scan_id;
        done.event_count = event_count;
        done.duration_ms = (uint16_t)(now_ms() - start);
        done.status      = LINK_WIFI_STATUS_OK;
        link_send_wifi_probe_done(&done);

        ESP_LOGI(TAG, "sniff id=%" PRIu32 " done events=%u elapsed=%ums "
                      "(cb total=%" PRIu32 " mgmt=%" PRIu32 " probe=%" PRIu32
                      " 2g=%" PRIu32 " 5g=%" PRIu32 ")",
                 req.scan_id, event_count, done.duration_ms,
                 s_diag_cb_total, s_diag_cb_mgmt, s_diag_cb_probe,
                 s_diag_cb_probe_2g, s_diag_cb_probe_5g);
    }
}

static void on_probe_req(const struct link_wifi_probe_req *req) {
    if (xQueueSend(s_req_queue, req, 0) != pdTRUE) {
        struct link_wifi_probe_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_WIFI_STATUS_ERROR;
        link_send_wifi_probe_done(&done);
        ESP_LOGE(TAG, "WIFI_PROBE_REQ id=%" PRIu32 " queue full", req->scan_id);
    }
}

void wifi_sniff_init(void) {
    s_req_queue   = xQueueCreate(SNIFF_REQ_QUEUE_LEN,   sizeof(struct link_wifi_probe_req));
    s_frame_queue = xQueueCreate(SNIFF_FRAME_QUEUE_LEN, sizeof(captured_probe_t));
    configASSERT(s_req_queue && s_frame_queue);

    link_register_wifi_probe_req(on_probe_req);

    xTaskCreate(wifi_sniff_task, "wifi_sniff", SNIFF_TASK_STACK,
                NULL, SNIFF_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "init promiscuous probe sniffer");
}
