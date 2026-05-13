#include "link.h"

#include <inttypes.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "hardware.h"
#include "link_codec.h"
#include "link_protocol.h"

static const char *TAG = "link";

#define LINK_FW_VERSION         0x0100u   // v1.0 stage 2
#define LINK_RX_BUF_SIZE        1024
#define LINK_TASK_STACK         4096
#define LINK_TASK_PRIORITY      10
#define LINK_TX_LOCK_TIMEOUT    pdMS_TO_TICKS(100)
#define LINK_PING_INTERVAL_MS   5000

static link_decoder_t   s_decoder;
static SemaphoreHandle_t s_tx_lock;
static uint8_t           s_seq;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void send_frame(uint8_t type, uint8_t seq,
                       const uint8_t *payload, size_t len) {
    if (xSemaphoreTake(s_tx_lock, LINK_TX_LOCK_TIMEOUT) != pdTRUE) {
        ESP_LOGW(TAG, "tx lock timeout, dropping type=0x%02x", type);
        return;
    }
    uint8_t buf[LINK_MAX_ENCODED];
    const size_t n = link_encode(type, seq, payload, len, buf, sizeof(buf));
    if (n > 0) {
        uart_write_bytes(HALBERD_C5_LINK_UART_NUM, (const char *)buf, n);
    } else {
        ESP_LOGE(TAG, "encode failed: type=0x%02x len=%u", type, (unsigned)len);
    }
    xSemaphoreGive(s_tx_lock);
}

void link_send_ping(void) {
    struct link_ping_payload p = { .uptime_ms = now_ms() };
    send_frame(LINK_MSG_PING, s_seq++, (const uint8_t *)&p, sizeof(p));
}

void link_send_gps_fix(const struct link_gps_fix *fix) {
    if (!fix) {
        return;
    }
    send_frame(LINK_MSG_GPS_FIX, s_seq++,
               (const uint8_t *)fix, sizeof(*fix));
}

void link_send_status(void) {
    const uint32_t errs = s_decoder.stats.bad_crc + s_decoder.stats.bad_length +
                          s_decoder.stats.short_frame + s_decoder.stats.overflow;
    struct link_status_payload p = {
        .uptime_ms    = now_ms(),
        .free_heap    = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
        .fw_version   = LINK_FW_VERSION,
        .rx_frame_ok  = (uint16_t)(s_decoder.stats.ok & 0xFFFFu),
        .rx_frame_err = (uint16_t)(errs & 0xFFFFu),
        .reserved     = 0,
    };
    send_frame(LINK_MSG_STATUS, s_seq++, (const uint8_t *)&p, sizeof(p));
}

void link_log_stats(void) {
    ESP_LOGI(TAG, "stats ok=%" PRIu32 " crc=%" PRIu32 " len=%" PRIu32
                  " short=%" PRIu32 " ovf=%" PRIu32,
             s_decoder.stats.ok, s_decoder.stats.bad_crc,
             s_decoder.stats.bad_length, s_decoder.stats.short_frame,
             s_decoder.stats.overflow);
}

static void on_frame(void *ctx, uint8_t type, uint8_t seq,
                     const uint8_t *payload, size_t len) {
    (void)ctx;
    switch (type) {
    case LINK_MSG_PING:
        // Reply with PONG carrying the peer's payload verbatim so the peer
        // can compute its own RTT.
        send_frame(LINK_MSG_PONG, seq, payload, len);
        if (len == sizeof(struct link_ping_payload)) {
            struct link_ping_payload p;
            memcpy(&p, payload, sizeof(p));
            ESP_LOGD(TAG, "PING seq=%u peer_uptime=%" PRIu32 "ms", seq, p.uptime_ms);
        }
        break;

    case LINK_MSG_PONG:
        if (len == sizeof(struct link_ping_payload)) {
            struct link_ping_payload p;
            memcpy(&p, payload, sizeof(p));
            ESP_LOGI(TAG, "PONG seq=%u rtt=%" PRIu32 "ms", seq, now_ms() - p.uptime_ms);
        } else {
            ESP_LOGW(TAG, "PONG seq=%u with unexpected len=%u", seq, (unsigned)len);
        }
        break;

    case LINK_MSG_STATUS:
        if (len == sizeof(struct link_status_payload)) {
            struct link_status_payload p;
            memcpy(&p, payload, sizeof(p));
            ESP_LOGI(TAG, "peer STATUS uptime=%" PRIu32 "ms heap=%" PRIu32
                          " fw=0x%04x rx_ok=%u rx_err=%u",
                     p.uptime_ms, p.free_heap, p.fw_version,
                     p.rx_frame_ok, p.rx_frame_err);
        }
        break;

    default:
        ESP_LOGW(TAG, "unknown type=0x%02x seq=%u len=%u",
                 type, seq, (unsigned)len);
        break;
    }
}

// ── Boot-time codec selftest ───────────────────────────────────────────────
// Without an S3 peer wired up we can't prove the link end-to-end, but we can
// at least catch arithmetic mistakes in the codec by round-tripping a known
// frame (with embedded zeros, to exercise COBS) through encode → decode on
// boot.
static bool     s_selftest_ok;
static uint8_t  s_selftest_type;
static uint8_t  s_selftest_seq;
static uint8_t  s_selftest_payload[64];
static size_t   s_selftest_len;

static void selftest_cb(void *ctx, uint8_t type, uint8_t seq,
                        const uint8_t *p, size_t l) {
    (void)ctx;
    s_selftest_ok = true;
    s_selftest_type = type;
    s_selftest_seq = seq;
    if (l <= sizeof(s_selftest_payload)) {
        memcpy(s_selftest_payload, p, l);
    }
    s_selftest_len = l;
}

static void selftest(void) {
    // Payload deliberately contains 0x00 bytes so COBS gets exercised.
    static const uint8_t want[] = { 0x00, 0xFF, 0x42, 0x00, 0x01, 0xAA };
    uint8_t enc[LINK_MAX_ENCODED];
    const size_t enc_len = link_encode(LINK_MSG_PING, 0x42,
                                       want, sizeof(want), enc, sizeof(enc));
    if (enc_len == 0) {
        ESP_LOGE(TAG, "selftest: encode failed");
        return;
    }

    link_decoder_t d;
    link_decoder_init(&d);
    s_selftest_ok = false;
    link_decoder_feed(&d, enc, enc_len, selftest_cb, NULL);

    if (!s_selftest_ok) {
        ESP_LOGE(TAG, "selftest: no frame decoded "
                      "(crc=%" PRIu32 " len=%" PRIu32 " short=%" PRIu32 ")",
                 d.stats.bad_crc, d.stats.bad_length, d.stats.short_frame);
        return;
    }
    const bool match = (s_selftest_type == LINK_MSG_PING) &&
                       (s_selftest_seq  == 0x42) &&
                       (s_selftest_len  == sizeof(want)) &&
                       (memcmp(s_selftest_payload, want, sizeof(want)) == 0);
    if (!match) {
        ESP_LOGE(TAG, "selftest: roundtrip mismatch (type=0x%02x seq=0x%02x len=%u)",
                 s_selftest_type, s_selftest_seq, (unsigned)s_selftest_len);
        return;
    }
    ESP_LOGI(TAG, "selftest OK (raw=%u bytes, encoded=%u bytes)",
             (unsigned)sizeof(want), (unsigned)enc_len);
}

static void link_task(void *arg) {
    (void)arg;
    uint8_t rx[256];
    TickType_t last_ping = xTaskGetTickCount();
    const TickType_t ping_interval = pdMS_TO_TICKS(LINK_PING_INTERVAL_MS);

    while (1) {
        const int got = uart_read_bytes(HALBERD_C5_LINK_UART_NUM,
                                        rx, sizeof(rx),
                                        pdMS_TO_TICKS(100));
        if (got > 0) {
            link_decoder_feed(&s_decoder, rx, (size_t)got, on_frame, NULL);
        }
        if ((xTaskGetTickCount() - last_ping) >= ping_interval) {
            link_send_ping();
            last_ping = xTaskGetTickCount();
        }
    }
}

void link_init(void) {
    s_tx_lock = xSemaphoreCreateMutex();
    link_decoder_init(&s_decoder);

    selftest();

    const uart_config_t cfg = {
        .baud_rate  = HALBERD_C5_LINK_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(HALBERD_C5_LINK_UART_NUM,
                                        LINK_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HALBERD_C5_LINK_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(HALBERD_C5_LINK_UART_NUM,
                                 HALBERD_C5_LINK_TX_GPIO,
                                 HALBERD_C5_LINK_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(link_task, "link", LINK_TASK_STACK, NULL, LINK_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "init UART%d tx=%d rx=%d baud=%d",
             HALBERD_C5_LINK_UART_NUM,
             HALBERD_C5_LINK_TX_GPIO, HALBERD_C5_LINK_RX_GPIO,
             HALBERD_C5_LINK_BAUD);
}
