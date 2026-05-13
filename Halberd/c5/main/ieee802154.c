#include "ieee802154.h"

#include <inttypes.h>
#include <string.h>

#include "esp_ieee802154.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "link.h"
#include "link_protocol.h"

static const char *TAG = "ieee802154";

#define IEEE_SCAN_QUEUE_LEN          4
#define IEEE_FRAME_QUEUE_LEN        32
#define IEEE_SCAN_TASK_STACK      4096
#define IEEE_SCAN_TASK_PRIORITY      6

// Per-channel dwell budget. 16 channels * 100 ms = 1.6 s for a full sweep.
#define IEEE_MIN_DWELL_MS_PER_CH    50u
#define IEEE_MAX_DWELL_MS_PER_CH   500u

// Raw bytes carried from the IRQ-context receive callback to the scan task.
// data[] is the PSDU (MAC frame) without the leading PHR length byte.
typedef struct {
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  lqi;
    uint8_t  data_len;            // 1..127, IEEE 802.15.4 PSDU max
    uint8_t  data[127];
} captured_frame_t;

static QueueHandle_t s_req_queue;
static QueueHandle_t s_frame_queue;
static volatile bool s_scan_busy;
static volatile uint8_t s_current_channel;

// Weak override fires from the 802.15.4 driver when a frame is received.
// IDF documents this as ISR-context. Keep the work minimal: copy bytes
// and push onto a queue.
void esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *info) {
    if (!s_scan_busy || frame == NULL || info == NULL) {
        esp_ieee802154_receive_handle_done(frame);
        return;
    }
    captured_frame_t f;
    f.channel = s_current_channel;
    f.rssi    = info->rssi;
    f.lqi     = info->lqi;
    f.data_len = frame[0];        // PHR length byte
    if (f.data_len > 127) f.data_len = 127;
    if (f.data_len > 0) memcpy(f.data, frame + 1, f.data_len);

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_frame_queue, &f, &woken);
    esp_ieee802154_receive_handle_done(frame);
    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Heuristic protocol-family classifier. See link_protocol.h for the enum.
//
// Zigbee: beacon frame whose payload starts with 0x00 (Protocol ID) and a
// stack profile byte of 0x01 (Zigbee 2007) or 0x02 (Zigbee Pro). The
// beacon payload lives after the superframe spec (2 B), GTS spec (1 B,
// usually 0x00), pending addr spec (1 B, usually 0x00).
//
// Thread: identification at the MAC layer is fuzzy. Thread doesn't use
// classical beacons; it uses MLE Discovery Request/Response (command
// frames) with TLV-encoded payloads. Heuristic: command frame with
// command identifier 0xFF (vendor-specific, used by some Thread builds)
// or data frame with PAN ID 0xFFFF and a 6LoWPAN dispatch byte in the
// payload (0x40-0x7F range = LOWPAN_IPHC / IPv6 header compression).
//
// Matter: same MAC-layer view as Thread (built on Thread). Distinguished
// at higher layers; we report MATTER only when we can spot a Matter-
// specific TLV / commissioning signature in the payload. Otherwise it
// falls into THREAD or OTHER.
static uint8_t classify_proto(uint8_t frame_type, const uint8_t *payload,
                              uint8_t payload_len, uint16_t src_pan, uint16_t dst_pan) {
    if (frame_type == LINK_IEEE_FRAME_BEACON && payload_len >= 6) {
        // After superframe (2) + GTS spec (1) + pending addr spec (1) we
        // expect the beacon payload. The first byte should be Protocol ID
        // (0x00 for Zigbee), the second contains Stack Profile in the
        // low nibble.
        uint8_t proto_id    = payload[4];
        uint8_t stack_profile = payload[5] & 0x0F;
        if (proto_id == 0x00 && (stack_profile == 0x01 || stack_profile == 0x02)) {
            return LINK_IEEE_PROTO_ZIGBEE;
        }
    }

    if (frame_type == LINK_IEEE_FRAME_DATA && payload_len >= 1) {
        // 6LoWPAN dispatch byte ranges:
        //   0x40..0x7F = LOWPAN_IPHC (IPv6 header compression, used by Thread/Matter)
        //   0x80..0xBF = mesh-routing prefix
        //   0xC0..0xFF = fragmentation header
        uint8_t disp = payload[0];
        if (disp >= 0x40 && disp <= 0x7F) {
            // Matter discriminator: Matter operational frames often use
            // specific Thread network credentials; without parsing CoAP /
            // DTLS we can't distinguish reliably. Default to THREAD; the
            // S3 / diginode-cc layer can promote to MATTER on deeper
            // inspection.
            return LINK_IEEE_PROTO_THREAD;
        }
    }

    if (frame_type == LINK_IEEE_FRAME_CMD && payload_len >= 1) {
        // MLE Discovery Request / Response identifiers — actual MLE
        // payloads ride inside data frames, but command-frame heuristics
        // for Thread joining tend to use known PAN ID 0xFFFF.
        if (src_pan == 0xFFFF || dst_pan == 0xFFFF) {
            return LINK_IEEE_PROTO_THREAD;
        }
    }

    return LINK_IEEE_PROTO_UNKNOWN;
}

// Parse a captured frame into a link_ieee_detection. Returns true if the
// frame was at least syntactically plausible (had a valid FCF + complete
// addressing fields); the caller emits the detection regardless of the
// classification verdict.
static bool parse_frame(const captured_frame_t *cap, uint32_t scan_id,
                        struct link_ieee_detection *out) {
    if (cap->data_len < 2) return false;

    const uint8_t *p = cap->data;
    size_t remaining = cap->data_len;

    // FCF (2 bytes, little-endian).
    uint16_t fcf = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    p += 2; remaining -= 2;

    uint8_t frame_type        = fcf & 0x07;
    bool    security_enabled  = (fcf >> 3) & 1;
    bool    frame_pending     = (fcf >> 4) & 1;
    bool    ack_request       = (fcf >> 5) & 1;
    bool    pan_id_compression = (fcf >> 6) & 1;
    bool    seq_suppression   = (fcf >> 8) & 1;
    bool    ie_present        = (fcf >> 9) & 1;
    uint8_t dst_addr_mode     = (fcf >> 10) & 0x03;
    uint8_t frame_version     = (fcf >> 12) & 0x03;
    uint8_t src_addr_mode     = (fcf >> 14) & 0x03;

    // Sequence number (1 byte unless suppression bit is set in 2015 frames).
    uint8_t seq_num = 0;
    if (!seq_suppression) {
        if (remaining < 1) return false;
        seq_num = *p;
        p += 1; remaining -= 1;
    }

    uint16_t dst_pan = 0xFFFF;
    uint8_t  dst_addr[LINK_IEEE_ADDR_LEN] = {0};
    if (dst_addr_mode == 2 || dst_addr_mode == 3) {
        if (remaining < 2) return false;
        dst_pan = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        p += 2; remaining -= 2;
        size_t alen = (dst_addr_mode == 2) ? 2 : 8;
        if (remaining < alen) return false;
        memcpy(dst_addr, p, alen);
        p += alen; remaining -= alen;
    }

    uint16_t src_pan = 0xFFFF;
    uint8_t  src_addr[LINK_IEEE_ADDR_LEN] = {0};
    if (src_addr_mode == 2 || src_addr_mode == 3) {
        if (!pan_id_compression) {
            if (remaining < 2) return false;
            src_pan = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            p += 2; remaining -= 2;
        } else {
            src_pan = dst_pan;     // implicit
        }
        size_t alen = (src_addr_mode == 2) ? 2 : 8;
        if (remaining < alen) return false;
        memcpy(src_addr, p, alen);
        p += alen; remaining -= alen;
    }

    // The PSDU usually carries a 2-byte FCS at the tail that the radio
    // strips for us — but if not, drop it from the payload view we report.
    size_t payload_len = remaining;
    if (payload_len >= 2) {
        // No reliable bit to know if FCS is present; ESP-IDF docs say the
        // driver delivers frames *without* FCS when configured for
        // promiscuous receive. We trust that.
    }
    if (payload_len > LINK_IEEE_PAYLOAD_MAX) payload_len = LINK_IEEE_PAYLOAD_MAX;

    out->scan_id        = scan_id;
    out->channel        = cap->channel;
    out->rssi           = cap->rssi;
    out->lqi            = cap->lqi;
    out->frame_type     = frame_type;
    out->frame_version  = frame_version;
    out->seq_num        = seq_num;
    out->dst_pan        = dst_pan;
    out->src_pan        = src_pan;
    out->dst_addr_mode  = dst_addr_mode;
    out->src_addr_mode  = src_addr_mode;
    memcpy(out->dst_addr, dst_addr, LINK_IEEE_ADDR_LEN);
    memcpy(out->src_addr, src_addr, LINK_IEEE_ADDR_LEN);
    out->payload_len    = (uint8_t)payload_len;
    memset(out->payload, 0, sizeof(out->payload));
    if (payload_len > 0) memcpy(out->payload, p, payload_len);

    out->flags = 0;
    if (security_enabled)  out->flags |= (1u << 0);
    if (ack_request)       out->flags |= (1u << 1);
    if (frame_pending)     out->flags |= (1u << 2);
    if (pan_id_compression) out->flags |= (1u << 3);
    if (seq_suppression)   out->flags |= (1u << 4);
    if (ie_present)        out->flags |= (1u << 5);

    out->protocol_family = classify_proto(frame_type, p, (uint8_t)payload_len,
                                          src_pan, dst_pan);
    return true;
}

static void scan_channel(uint8_t channel, uint32_t dwell_ms, uint32_t scan_id,
                         uint16_t *count) {
    s_current_channel = channel;
    esp_err_t err = esp_ieee802154_set_channel(channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_channel %u: %s", channel, esp_err_to_name(err));
        return;
    }
    err = esp_ieee802154_receive();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "receive ch=%u: %s", channel, esp_err_to_name(err));
        return;
    }

    uint32_t deadline = now_ms() + dwell_ms;
    captured_frame_t cap;
    while (now_ms() < deadline) {
        TickType_t wait = pdMS_TO_TICKS(20);
        if (xQueueReceive(s_frame_queue, &cap, wait) != pdTRUE) continue;

        struct link_ieee_detection det;
        memset(&det, 0, sizeof(det));
        if (parse_frame(&cap, scan_id, &det)) {
            link_send_ieee_detection(&det);
            *count += 1;
        }
    }
}

static void ieee_scan_task(void *arg) {
    (void)arg;
    struct link_ieee_scan_req req;

    while (1) {
        if (xQueueReceive(s_req_queue, &req, portMAX_DELAY) != pdTRUE) continue;
        if (req.channel_count == 0 || req.channel_count > LINK_IEEE_CHANNEL_COUNT_MAX) {
            struct link_ieee_scan_done done = {0};
            done.scan_id = req.scan_id;
            done.status  = LINK_IEEE_STATUS_ERROR;
            link_send_ieee_scan_done(&done);
            continue;
        }

        s_scan_busy = true;

        uint32_t dwell = req.duration_ms / req.channel_count;
        if (dwell < IEEE_MIN_DWELL_MS_PER_CH) dwell = IEEE_MIN_DWELL_MS_PER_CH;
        if (dwell > IEEE_MAX_DWELL_MS_PER_CH) dwell = IEEE_MAX_DWELL_MS_PER_CH;

        uint32_t start = now_ms();
        uint16_t detection_count = 0;

        ESP_LOGI(TAG, "scan id=%" PRIu32 " ch_count=%u dwell=%" PRIu32 "ms",
                 req.scan_id, req.channel_count, dwell);

        for (uint8_t i = 0; i < req.channel_count; i++) {
            uint8_t ch = req.channels[i];
            if (ch < 11 || ch > 26) continue;
            scan_channel(ch, dwell, req.scan_id, &detection_count);
        }

        // Drain any straggler frames so the next scan starts clean.
        captured_frame_t drop;
        while (xQueueReceive(s_frame_queue, &drop, 0) == pdTRUE) { /* discard */ }

        struct link_ieee_scan_done done = {0};
        done.scan_id          = req.scan_id;
        done.detection_count  = detection_count;
        done.duration_ms      = (uint16_t)(now_ms() - start);
        done.status           = LINK_IEEE_STATUS_OK;
        link_send_ieee_scan_done(&done);

        ESP_LOGI(TAG, "scan id=%" PRIu32 " done detections=%u elapsed=%ums",
                 req.scan_id, detection_count, done.duration_ms);

        s_scan_busy = false;
    }
}

static void on_scan_req(const struct link_ieee_scan_req *req) {
    if (s_scan_busy) {
        struct link_ieee_scan_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_IEEE_STATUS_BUSY;
        link_send_ieee_scan_done(&done);
        ESP_LOGW(TAG, "IEEE_SCAN_REQ id=%" PRIu32 " rejected: BUSY", req->scan_id);
        return;
    }
    if (xQueueSend(s_req_queue, req, 0) != pdTRUE) {
        struct link_ieee_scan_done done = {0};
        done.scan_id = req->scan_id;
        done.status  = LINK_IEEE_STATUS_ERROR;
        link_send_ieee_scan_done(&done);
        ESP_LOGE(TAG, "IEEE_SCAN_REQ id=%" PRIu32 " queue full", req->scan_id);
    }
}

void ieee802154_init(void) {
    esp_err_t err = esp_ieee802154_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable: %s", esp_err_to_name(err));
        return;
    }
    // Promiscuous: deliver every frame regardless of address filtering;
    // matches the sniffer mission.
    esp_ieee802154_set_promiscuous(true);
    esp_ieee802154_set_rx_when_idle(true);

    s_req_queue   = xQueueCreate(IEEE_SCAN_QUEUE_LEN,   sizeof(struct link_ieee_scan_req));
    s_frame_queue = xQueueCreate(IEEE_FRAME_QUEUE_LEN,  sizeof(captured_frame_t));
    configASSERT(s_req_queue && s_frame_queue);

    link_register_ieee_scan_req(on_scan_req);

    xTaskCreate(ieee_scan_task, "ieee_scan", IEEE_SCAN_TASK_STACK,
                NULL, IEEE_SCAN_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "init promiscuous RX, ch 11-26");
}
