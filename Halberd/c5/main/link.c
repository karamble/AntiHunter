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

static link_wifi_scan_req_cb s_wifi_scan_req_cb;

void link_register_wifi_scan_req(link_wifi_scan_req_cb cb) {
    s_wifi_scan_req_cb = cb;
}

void link_send_wifi_ap_result(const struct link_wifi_ap_result *r) {
    if (!r) return;
    send_frame(LINK_MSG_WIFI_AP_RESULT, s_seq++,
               (const uint8_t *)r, sizeof(*r));
}

void link_send_wifi_scan_done(const struct link_wifi_scan_done *d) {
    if (!d) return;
    send_frame(LINK_MSG_WIFI_SCAN_DONE, s_seq++,
               (const uint8_t *)d, sizeof(*d));
}

static link_wifi_probe_req_cb s_wifi_probe_req_cb;

void link_register_wifi_probe_req(link_wifi_probe_req_cb cb) {
    s_wifi_probe_req_cb = cb;
}

void link_send_wifi_probe_event(const struct link_wifi_probe_event *ev) {
    if (!ev) return;
    send_frame(LINK_MSG_WIFI_PROBE_EVENT, s_seq++,
               (const uint8_t *)ev, sizeof(*ev));
}

void link_send_wifi_probe_done(const struct link_wifi_probe_done *done) {
    if (!done) return;
    send_frame(LINK_MSG_WIFI_PROBE_DONE, s_seq++,
               (const uint8_t *)done, sizeof(*done));
}

static link_ble_scan_req_cb s_ble_scan_req_cb;

void link_register_ble_scan_req(link_ble_scan_req_cb cb) {
    s_ble_scan_req_cb = cb;
}

void link_send_ble_adv(const struct link_ble_adv *adv) {
    if (!adv) return;
    send_frame(LINK_MSG_BLE_ADV, s_seq++,
               (const uint8_t *)adv, sizeof(*adv));
}

void link_send_ble_scan_done(const struct link_ble_scan_done *done) {
    if (!done) return;
    send_frame(LINK_MSG_BLE_SCAN_DONE, s_seq++,
               (const uint8_t *)done, sizeof(*done));
}

static link_ieee_scan_req_cb s_ieee_scan_req_cb;

void link_register_ieee_scan_req(link_ieee_scan_req_cb cb) {
    s_ieee_scan_req_cb = cb;
}

void link_send_ieee_detection(const struct link_ieee_detection *det) {
    if (!det) return;
    send_frame(LINK_MSG_IEEE_DETECTION, s_seq++,
               (const uint8_t *)det, sizeof(*det));
}

void link_send_ieee_scan_done(const struct link_ieee_scan_done *done) {
    if (!done) return;
    send_frame(LINK_MSG_IEEE_SCAN_DONE, s_seq++,
               (const uint8_t *)done, sizeof(*done));
}

static link_i2c_read_req_cb  s_i2c_read_req_cb;
static link_i2c_write_req_cb s_i2c_write_req_cb;
static link_gpio_req_cb      s_gpio_req_cb;

void link_register_i2c_read_req(link_i2c_read_req_cb cb)   { s_i2c_read_req_cb  = cb; }
void link_register_i2c_write_req(link_i2c_write_req_cb cb) { s_i2c_write_req_cb = cb; }
void link_register_gpio_req(link_gpio_req_cb cb)           { s_gpio_req_cb      = cb; }

void link_send_i2c_read_resp(const struct link_i2c_read_resp *resp) {
    if (!resp) return;
    send_frame(LINK_MSG_I2C_READ_RESP, s_seq++,
               (const uint8_t *)resp, sizeof(*resp));
}

void link_send_i2c_write_resp(const struct link_i2c_write_resp *resp) {
    if (!resp) return;
    send_frame(LINK_MSG_I2C_WRITE_RESP, s_seq++,
               (const uint8_t *)resp, sizeof(*resp));
}

void link_send_gpio_resp(const struct link_gpio_resp *resp) {
    if (!resp) return;
    send_frame(LINK_MSG_GPIO_RESP, s_seq++,
               (const uint8_t *)resp, sizeof(*resp));
}

void link_send_sensor_event(const struct link_sensor_event *ev) {
    if (!ev) return;
    send_frame(LINK_MSG_SENSOR_EVENT, s_seq++,
               (const uint8_t *)ev, sizeof(*ev));
}

// ── SD-proxy client (C5 → S3 requests) ─────────────────────────────────────
//
// Mirror of the S3-side I²C bridge in the opposite direction: one
// outstanding SD op at a time, request_id correlation, blocking caller
// woken by the link RX task when the matching RESP arrives.
//
// Timeout matches the worst-case SD-card stall the FatFs driver tolerates
// before its own retry path kicks in — 2 s is generous for a healthy card
// and avoids stranding a driver task on a card that has gone away.

#define LINK_SD_REQUEST_TIMEOUT_MS 2000

static SemaphoreHandle_t       s_sd_mutex;
static SemaphoreHandle_t       s_sd_resp_sem;
static uint32_t                s_sd_pending_id;
static uint8_t                 s_sd_pending_type;   // expected RESP type, 0 = no op pending
static struct link_sd_read_resp  s_sd_read_resp_slot;
static struct link_sd_write_resp s_sd_write_resp_slot;
static struct link_sd_stat_resp  s_sd_stat_resp_slot;
static uint32_t                s_sd_next_request_id = 1;

static void sd_init_locked_state(void) {
    if (s_sd_mutex == NULL) {
        s_sd_mutex    = xSemaphoreCreateMutex();
        s_sd_resp_sem = xSemaphoreCreateBinary();
    }
}

// Called from on_frame() under the link RX task when a SD_*_RESP frame
// arrives. Routes the bytes into the matching slot and wakes the caller.
// Stale responses (no pending op, or wrong request_id) are dropped.
static void sd_deliver_response(uint8_t type, const uint8_t *payload, size_t len) {
    if (s_sd_pending_type != type) {
        ESP_LOGW(TAG, "SD resp type=0x%02x dropped (pending=0x%02x)",
                 type, s_sd_pending_type);
        return;
    }
    switch (type) {
    case LINK_MSG_SD_READ_RESP:
        if (len != sizeof(s_sd_read_resp_slot)) goto bad_len;
        memcpy(&s_sd_read_resp_slot, payload, sizeof(s_sd_read_resp_slot));
        if (s_sd_read_resp_slot.request_id != s_sd_pending_id) goto bad_id;
        break;
    case LINK_MSG_SD_WRITE_RESP:
        if (len != sizeof(s_sd_write_resp_slot)) goto bad_len;
        memcpy(&s_sd_write_resp_slot, payload, sizeof(s_sd_write_resp_slot));
        if (s_sd_write_resp_slot.request_id != s_sd_pending_id) goto bad_id;
        break;
    case LINK_MSG_SD_STAT_RESP:
        if (len != sizeof(s_sd_stat_resp_slot)) goto bad_len;
        memcpy(&s_sd_stat_resp_slot, payload, sizeof(s_sd_stat_resp_slot));
        if (s_sd_stat_resp_slot.request_id != s_sd_pending_id) goto bad_id;
        break;
    default:
        return;
    }
    xSemaphoreGive(s_sd_resp_sem);
    return;
bad_len:
    ESP_LOGW(TAG, "SD resp type=0x%02x bad len=%u", type, (unsigned)len);
    return;
bad_id:
    ESP_LOGW(TAG, "SD resp type=0x%02x stale id, dropping", type);
}

// Internal request helper used by all three public SD calls. Acquires the
// SD mutex, sends the REQ frame, blocks on the response semaphore, returns
// 0 on a clean response. Negative returns: -1 = mutex timeout (another
// SD op in progress longer than the timeout), -2 = response timeout
// (S3 didn't reply), -3 = state init failure.
static int sd_request_locked(uint8_t req_type, uint8_t want_resp_type,
                             uint32_t request_id,
                             const uint8_t *req_bytes, size_t req_len) {
    sd_init_locked_state();
    if (s_sd_mutex == NULL || s_sd_resp_sem == NULL) {
        return -3;
    }
    const TickType_t timeout = pdMS_TO_TICKS(LINK_SD_REQUEST_TIMEOUT_MS);
    if (xSemaphoreTake(s_sd_mutex, timeout) != pdTRUE) {
        ESP_LOGW(TAG, "SD mutex timeout for req=0x%02x", req_type);
        return -1;
    }
    // Drain any stale signal from a prior timed-out call before arming the
    // wait — otherwise a late RESP from the previous call would falsely
    // satisfy this one.
    xSemaphoreTake(s_sd_resp_sem, 0);
    s_sd_pending_id   = request_id;
    s_sd_pending_type = want_resp_type;

    send_frame(req_type, s_seq++, req_bytes, req_len);

    int rc = 0;
    if (xSemaphoreTake(s_sd_resp_sem, timeout) != pdTRUE) {
        ESP_LOGW(TAG, "SD resp timeout for req=0x%02x id=%" PRIu32,
                 req_type, request_id);
        rc = -2;
    }
    s_sd_pending_type = 0;
    xSemaphoreGive(s_sd_mutex);
    return rc;
}

int link_sd_read(const char *path, uint32_t offset,
                 uint8_t *out_buf, uint16_t buf_cap,
                 uint16_t *out_len, uint8_t *out_eof,
                 uint8_t *out_status) {
    if (!path || !out_buf || buf_cap == 0) return -3;
    const size_t plen = strnlen(path, LINK_SD_PATH_MAX + 1);
    if (plen == 0 || plen > LINK_SD_PATH_MAX) return -3;

    struct link_sd_read_req req = {
        .request_id = s_sd_next_request_id++,
        .offset     = offset,
        .read_len   = (buf_cap > LINK_SD_DATA_MAX) ? LINK_SD_DATA_MAX : buf_cap,
        .path_len   = (uint8_t)plen,
        .reserved   = 0,
    };
    memcpy(req.path, path, plen);

    int rc = sd_request_locked(LINK_MSG_SD_READ_REQ, LINK_MSG_SD_READ_RESP,
                               req.request_id,
                               (const uint8_t *)&req, sizeof(req));
    if (rc != 0) return rc;

    if (out_status) *out_status = s_sd_read_resp_slot.status;
    if (s_sd_read_resp_slot.status != LINK_SD_STATUS_OK) {
        if (out_len) *out_len = 0;
        if (out_eof) *out_eof = 0;
        return 0;
    }
    const uint16_t n = (s_sd_read_resp_slot.data_len > buf_cap)
                          ? buf_cap : s_sd_read_resp_slot.data_len;
    memcpy(out_buf, s_sd_read_resp_slot.data, n);
    if (out_len) *out_len = n;
    if (out_eof) *out_eof = s_sd_read_resp_slot.eof;
    return 0;
}

int link_sd_write(const char *path, uint32_t offset,
                  const uint8_t *data, uint16_t data_len,
                  uint8_t flags,
                  uint32_t *out_bytes_written,
                  uint8_t *out_status) {
    if (!path) return -3;
    if (data_len > LINK_SD_DATA_MAX) return -3;
    if (data_len > 0 && !data) return -3;
    const size_t plen = strnlen(path, LINK_SD_PATH_MAX + 1);
    if (plen == 0 || plen > LINK_SD_PATH_MAX) return -3;

    struct link_sd_write_req req = {
        .request_id = s_sd_next_request_id++,
        .offset     = offset,
        .path_len   = (uint8_t)plen,
        .flags      = flags,
        .data_len   = data_len,
    };
    memcpy(req.path, path, plen);
    if (data_len) memcpy(req.data, data, data_len);

    int rc = sd_request_locked(LINK_MSG_SD_WRITE_REQ, LINK_MSG_SD_WRITE_RESP,
                               req.request_id,
                               (const uint8_t *)&req, sizeof(req));
    if (rc != 0) return rc;

    if (out_status) *out_status = s_sd_write_resp_slot.status;
    if (out_bytes_written) *out_bytes_written = s_sd_write_resp_slot.bytes_written;
    return 0;
}

int link_sd_stat(const char *path,
                 uint32_t *out_size_bytes,
                 uint32_t *out_mtime_unix,
                 uint8_t *out_is_dir,
                 uint8_t *out_status) {
    if (!path) return -3;
    const size_t plen = strnlen(path, LINK_SD_PATH_MAX + 1);
    if (plen == 0 || plen > LINK_SD_PATH_MAX) return -3;

    struct link_sd_stat_req req = {
        .request_id = s_sd_next_request_id++,
        .path_len   = (uint8_t)plen,
        .reserved   = { 0, 0, 0 },
    };
    memcpy(req.path, path, plen);

    int rc = sd_request_locked(LINK_MSG_SD_STAT_REQ, LINK_MSG_SD_STAT_RESP,
                               req.request_id,
                               (const uint8_t *)&req, sizeof(req));
    if (rc != 0) return rc;

    if (out_status)     *out_status     = s_sd_stat_resp_slot.status;
    if (out_size_bytes) *out_size_bytes = s_sd_stat_resp_slot.size_bytes;
    if (out_mtime_unix) *out_mtime_unix = s_sd_stat_resp_slot.mtime_unix;
    if (out_is_dir)     *out_is_dir     = s_sd_stat_resp_slot.is_dir;
    return 0;
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

    case LINK_MSG_WIFI_SCAN_REQ:
        if (len == sizeof(struct link_wifi_scan_req) && s_wifi_scan_req_cb) {
            struct link_wifi_scan_req req;
            memcpy(&req, payload, sizeof(req));
            s_wifi_scan_req_cb(&req);
        } else {
            ESP_LOGW(TAG, "WIFI_SCAN_REQ seq=%u len=%u cb=%p — dropped",
                     seq, (unsigned)len, s_wifi_scan_req_cb);
        }
        break;

    case LINK_MSG_WIFI_PROBE_REQ:
        if (len == sizeof(struct link_wifi_probe_req) && s_wifi_probe_req_cb) {
            struct link_wifi_probe_req req;
            memcpy(&req, payload, sizeof(req));
            s_wifi_probe_req_cb(&req);
        } else {
            ESP_LOGW(TAG, "WIFI_PROBE_REQ seq=%u len=%u cb=%p — dropped",
                     seq, (unsigned)len, s_wifi_probe_req_cb);
        }
        break;

    case LINK_MSG_BLE_SCAN_REQ:
        if (len == sizeof(struct link_ble_scan_req) && s_ble_scan_req_cb) {
            struct link_ble_scan_req req;
            memcpy(&req, payload, sizeof(req));
            s_ble_scan_req_cb(&req);
        } else {
            ESP_LOGW(TAG, "BLE_SCAN_REQ seq=%u len=%u cb=%p — dropped",
                     seq, (unsigned)len, s_ble_scan_req_cb);
        }
        break;

    case LINK_MSG_IEEE_SCAN_REQ:
        if (len == sizeof(struct link_ieee_scan_req) && s_ieee_scan_req_cb) {
            struct link_ieee_scan_req req;
            memcpy(&req, payload, sizeof(req));
            s_ieee_scan_req_cb(&req);
        } else {
            ESP_LOGW(TAG, "IEEE_SCAN_REQ seq=%u len=%u cb=%p — dropped",
                     seq, (unsigned)len, s_ieee_scan_req_cb);
        }
        break;

    case LINK_MSG_I2C_READ_REQ:
        if (len == sizeof(struct link_i2c_read_req) && s_i2c_read_req_cb) {
            struct link_i2c_read_req req;
            memcpy(&req, payload, sizeof(req));
            s_i2c_read_req_cb(&req);
        }
        break;

    case LINK_MSG_I2C_WRITE_REQ:
        if (len == sizeof(struct link_i2c_write_req) && s_i2c_write_req_cb) {
            struct link_i2c_write_req req;
            memcpy(&req, payload, sizeof(req));
            s_i2c_write_req_cb(&req);
        }
        break;

    case LINK_MSG_GPIO_REQ:
        if (len == sizeof(struct link_gpio_req) && s_gpio_req_cb) {
            struct link_gpio_req req;
            memcpy(&req, payload, sizeof(req));
            s_gpio_req_cb(&req);
        }
        break;

    case LINK_MSG_SD_READ_RESP:
    case LINK_MSG_SD_WRITE_RESP:
    case LINK_MSG_SD_STAT_RESP:
        sd_deliver_response(type, payload, len);
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
