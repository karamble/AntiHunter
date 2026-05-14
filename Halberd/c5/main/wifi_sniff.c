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

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Promiscuous RX callback. Runs in ISR-ish context: keep work tight.
// Filters to 802.11 management frames, then to subtype 4 (probe request)
// or 5 (probe response). The rest get dropped.
static void probe_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (!s_sniff_busy || type != WIFI_PKT_MGMT || buf == NULL) return;

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

        // Serialise against the scan task (wifi.c). Non-blocking grab; if
        // a scan is in progress we surrender the job and the S3 re-arms.
        if (xSemaphoreTake(wifi_radio_mutex, 0) != pdTRUE) {
            struct link_wifi_probe_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_BUSY;
            link_send_wifi_probe_done(&done);
            ESP_LOGW(TAG, "sniff id=%" PRIu32 " rejected: radio busy",
                     req.scan_id);
            continue;
        }

        s_capture_responses = (req.capture_responses != 0);
        s_sniff_busy = true;

        // Drain any pre-existing junk in the frame queue before arming.
        captured_probe_t drop;
        while (xQueueReceive(s_frame_queue, &drop, 0) == pdTRUE) { }

        esp_err_t err = esp_wifi_set_promiscuous(true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "set_promiscuous(true): %s", esp_err_to_name(err));
            s_sniff_busy = false;
            xSemaphoreGive(wifi_radio_mutex);
            struct link_wifi_probe_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_WIFI_STATUS_ERROR;
            link_send_wifi_probe_done(&done);
            continue;
        }
        wifi_promiscuous_filter_t flt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
        esp_wifi_set_promiscuous_filter(&flt);
        esp_wifi_set_promiscuous_rx_cb(probe_rx_cb);

        uint32_t dwell = req.duration_ms / req.channel_count;
        if (dwell < SNIFF_MIN_DWELL_MS_PER_CH) dwell = SNIFF_MIN_DWELL_MS_PER_CH;
        if (dwell > SNIFF_MAX_DWELL_MS_PER_CH) dwell = SNIFF_MAX_DWELL_MS_PER_CH;

        uint32_t start = now_ms();
        uint16_t event_count = 0;

        ESP_LOGI(TAG, "sniff id=%" PRIu32 " ch_count=%u dwell=%" PRIu32
                      "ms cap_resp=%u",
                 req.scan_id, req.channel_count, dwell,
                 (unsigned)req.capture_responses);

        for (uint8_t i = 0; i < req.channel_count; i++) {
            uint8_t ch = req.channels[i];
            if (ch == 0) continue;
            sniff_one_channel(ch, dwell, req.scan_id, &event_count);
        }

        esp_wifi_set_promiscuous(false);
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

        ESP_LOGI(TAG, "sniff id=%" PRIu32 " done events=%u elapsed=%ums",
                 req.scan_id, event_count, done.duration_ms);
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
