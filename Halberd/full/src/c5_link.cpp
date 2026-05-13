#include "c5_link.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <string.h>

#include "hardware.h"
#include "link_codec.h"
#include "link_protocol.h"

namespace {

constexpr int      C5_LINK_UART_NUM     = 2;
constexpr uint32_t C5_LINK_BAUD         = 921600;
constexpr uint32_t C5_LINK_PING_MS      = 5000;
constexpr uint16_t C5_LINK_FW_VERSION   = 0x0100;  // S3 stage 2

// Wi-Fi scan result queue capacity. Each frame on the wire is ~49 bytes;
// a worst-case scan of 32 channels with ~30 APs each is ~960 entries.
// Realistic loads are much smaller; we cap at 64 to bound RAM (~3 KB).
constexpr size_t   C5_LINK_WIFI_QUEUE_LEN = 64;
// BLE adv queue. Adverts arrive faster than Wi-Fi APs (many per second
// per device); the scanner task drains at scan-loop rate, so we size
// for a few seconds of headroom. Each entry is ~80 B → ~10 KB.
constexpr size_t   C5_LINK_BLE_QUEUE_LEN = 128;
// 802.15.4 detections are lower-rate than BLE adverts (one network beacon
// per ~100 ms typically) but each carries the full parsed view + payload.
// 32 slots covers a few seconds of busy traffic.
constexpr size_t   C5_LINK_IEEE_QUEUE_LEN = 32;

HardwareSerial    s_uart(C5_LINK_UART_NUM);
link_decoder_t    s_decoder;
SemaphoreHandle_t s_tx_lock = nullptr;
uint8_t           s_seq = 0;
bool              s_initialized = false;
uint32_t          s_last_ping_ms = 0;
QueueHandle_t     s_wifi_queue = nullptr;
QueueHandle_t     s_ble_queue = nullptr;
QueueHandle_t     s_ieee_queue = nullptr;
volatile uint32_t s_wifi_done_scan_id = 0;
volatile uint32_t s_ble_done_scan_id = 0;
volatile uint32_t s_ieee_done_scan_id = 0;

// Expansion-bus single-outstanding-op slot (stage 7). A caller serialises
// via s_exp_lock, fills in s_exp_request_id, sends a *_REQ, then waits on
// s_exp_done. Handlers below populate the response payload via the
// appropriate s_exp_*_resp slot before signalling s_exp_done.
SemaphoreHandle_t s_exp_lock = nullptr;
SemaphoreHandle_t s_exp_done = nullptr;
volatile uint32_t s_exp_request_id = 0;
struct link_i2c_read_resp  s_exp_i2c_read_resp;
struct link_i2c_write_resp s_exp_i2c_write_resp;
struct link_gpio_resp      s_exp_gpio_resp;
volatile uint8_t           s_exp_expected_type = 0;  // wire LINK_MSG_*_RESP we're waiting for

void send_frame(uint8_t type, uint8_t seq,
                const uint8_t *payload, size_t len) {
    if (!s_initialized || s_tx_lock == nullptr) {
        return;
    }
    if (xSemaphoreTake(s_tx_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.printf("[c5link] tx lock timeout type=0x%02x\n", type);
        return;
    }
    uint8_t buf[LINK_MAX_ENCODED];
    size_t n = link_encode(type, seq, payload, len, buf, sizeof(buf));
    if (n > 0) {
        s_uart.write(buf, n);
    } else {
        Serial.printf("[c5link] encode failed type=0x%02x len=%u\n",
                      type, (unsigned)len);
    }
    xSemaphoreGive(s_tx_lock);
}

void apply_gps_fix(const struct link_gps_fix &fix) {
    // Single source of truth for the typed GPS globals declared in
    // hardware.h. updateGPSLocation() runs on the main loop and only does
    // freshness/timeout bookkeeping based on gpsLastFixMs.
    bool nowValid = (fix.fix_valid != 0);

    if (gpsMutex != nullptr && xSemaphoreTake(gpsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gpsLat = (float)fix.lat_e7 / 1.0e7f;
        gpsLon = (float)fix.lon_e7 / 1.0e7f;
        gpsValid = nowValid;
        xSemaphoreGive(gpsMutex);
    } else {
        gpsLat = (float)fix.lat_e7 / 1.0e7f;
        gpsLon = (float)fix.lon_e7 / 1.0e7f;
        gpsValid = nowValid;
    }

    gpsSatellites  = fix.satellites;
    gpsHDOP        = (fix.hdop_x100 == 0) ? 99.9f : (float)fix.hdop_x100 / 100.0f;
    gpsAltitudeM   = (int)fix.altitude_m;
    gpsYear        = fix.year;
    gpsMonth       = fix.month;
    gpsDay         = fix.day;
    gpsHour        = fix.hour;
    gpsMinute      = fix.minute;
    gpsSecond      = fix.second;
    gpsCentisecond = fix.centisecond;
    gpsDateValid   = (fix.date_valid != 0);
    gpsTimeValid   = (fix.time_valid != 0);
    gpsLastFixMs   = (uint32_t)millis();

    if (nowValid) {
        lastGPSData = "Lat: " + String(gpsLat, 6)
                    + ", Lon: " + String(gpsLon, 6)
                    + " (sats=" + String((int)gpsSatellites)
                    + " HDOP=" + String(gpsHDOP, 2) + ")";
    } else {
        lastGPSData = "No valid GPS fix (sats=" + String((int)gpsSatellites) + ")";
    }
}

void on_frame(void * /*ctx*/, uint8_t type, uint8_t seq,
              const uint8_t *payload, size_t len) {
    switch (type) {
    case LINK_MSG_PING:
        // Echo the peer's payload back so they can compute their RTT.
        send_frame(LINK_MSG_PONG, seq, payload, len);
        break;

    case LINK_MSG_PONG:
        if (len == sizeof(struct link_ping_payload)) {
            struct link_ping_payload p;
            memcpy(&p, payload, sizeof(p));
            uint32_t now = (uint32_t)millis();
            Serial.printf("[c5link] PONG seq=%u rtt=%u ms\n",
                          seq, (unsigned)(now - p.uptime_ms));
        } else {
            Serial.printf("[c5link] PONG seq=%u unexpected len=%u\n",
                          seq, (unsigned)len);
        }
        break;

    case LINK_MSG_GPS_FIX:
        if (len == sizeof(struct link_gps_fix)) {
            struct link_gps_fix fix;
            memcpy(&fix, payload, sizeof(fix));
            apply_gps_fix(fix);
        } else {
            Serial.printf("[c5link] GPS_FIX unexpected len=%u (want %u)\n",
                          (unsigned)len, (unsigned)sizeof(struct link_gps_fix));
        }
        break;

    case LINK_MSG_WIFI_AP_RESULT:
        if (len == sizeof(struct link_wifi_ap_result) && s_wifi_queue != nullptr) {
            struct link_wifi_ap_result r;
            memcpy(&r, payload, sizeof(r));
            C5WifiAp ap;
            ap.scan_id   = r.scan_id;
            ap.rssi      = r.rssi;
            ap.channel   = r.channel;
            memcpy(ap.bssid, r.bssid, 6);
            ap.auth_mode = r.auth_mode;
            ap.ssid_len  = r.ssid_len > 32 ? 32 : r.ssid_len;
            memcpy(ap.ssid, r.ssid, 32);
            ap.phy_modes = r.phy_modes;
            // Non-blocking: drop oldest by overwriting if queue is full.
            if (xQueueSend(s_wifi_queue, &ap, 0) != pdTRUE) {
                C5WifiAp drop;
                xQueueReceive(s_wifi_queue, &drop, 0);
                xQueueSend(s_wifi_queue, &ap, 0);
            }
        }
        break;

    case LINK_MSG_WIFI_SCAN_DONE:
        if (len == sizeof(struct link_wifi_scan_done)) {
            struct link_wifi_scan_done d;
            memcpy(&d, payload, sizeof(d));
            s_wifi_done_scan_id = d.scan_id;
            Serial.printf("[c5link] WIFI scan id=%u done aps=%u elapsed=%ums status=%u\n",
                          (unsigned)d.scan_id, d.ap_count, d.duration_ms, d.status);
        }
        break;

    case LINK_MSG_BLE_ADV:
        if (len == sizeof(struct link_ble_adv) && s_ble_queue != nullptr) {
            struct link_ble_adv a;
            memcpy(&a, payload, sizeof(a));
            C5BleAdv ev;
            ev.scan_id       = a.scan_id;
            memcpy(ev.addr, a.addr, 6);
            ev.addr_type     = a.addr_type;
            ev.rssi          = a.rssi;
            ev.primary_phy   = a.primary_phy;
            ev.secondary_phy = a.secondary_phy;
            ev.tx_power      = a.tx_power;
            ev.adv_type      = a.adv_type;
            ev.adv_data_len  = a.adv_data_len > C5_BLE_ADV_DATA_MAX ? C5_BLE_ADV_DATA_MAX : a.adv_data_len;
            memcpy(ev.adv_data, a.adv_data, C5_BLE_ADV_DATA_MAX);
            if (xQueueSend(s_ble_queue, &ev, 0) != pdTRUE) {
                C5BleAdv drop;
                xQueueReceive(s_ble_queue, &drop, 0);
                xQueueSend(s_ble_queue, &ev, 0);
            }
        }
        break;

    case LINK_MSG_BLE_SCAN_DONE:
        if (len == sizeof(struct link_ble_scan_done)) {
            struct link_ble_scan_done d;
            memcpy(&d, payload, sizeof(d));
            s_ble_done_scan_id = d.scan_id;
            Serial.printf("[c5link] BLE scan id=%u done advs=%u elapsed=%ums status=%u\n",
                          (unsigned)d.scan_id, d.adv_count, d.duration_ms, d.status);
        }
        break;

    case LINK_MSG_IEEE_DETECTION:
        if (len == sizeof(struct link_ieee_detection) && s_ieee_queue != nullptr) {
            struct link_ieee_detection w;
            memcpy(&w, payload, sizeof(w));
            C5Ieee802154Detection d;
            d.scan_id         = w.scan_id;
            d.channel         = w.channel;
            d.rssi            = w.rssi;
            d.lqi             = w.lqi;
            d.frame_type      = w.frame_type;
            d.frame_version   = w.frame_version;
            d.protocol_family = w.protocol_family;
            d.seq_num         = w.seq_num;
            d.flags           = w.flags;
            d.dst_pan         = w.dst_pan;
            d.src_pan         = w.src_pan;
            d.dst_addr_mode   = w.dst_addr_mode;
            d.src_addr_mode   = w.src_addr_mode;
            memcpy(d.dst_addr, w.dst_addr, C5_IEEE_ADDR_LEN);
            memcpy(d.src_addr, w.src_addr, C5_IEEE_ADDR_LEN);
            d.payload_len     = w.payload_len > C5_IEEE_PAYLOAD_MAX ? C5_IEEE_PAYLOAD_MAX : w.payload_len;
            memcpy(d.payload, w.payload, C5_IEEE_PAYLOAD_MAX);
            if (xQueueSend(s_ieee_queue, &d, 0) != pdTRUE) {
                C5Ieee802154Detection drop;
                xQueueReceive(s_ieee_queue, &drop, 0);
                xQueueSend(s_ieee_queue, &d, 0);
            }
        }
        break;

    case LINK_MSG_IEEE_SCAN_DONE:
        if (len == sizeof(struct link_ieee_scan_done)) {
            struct link_ieee_scan_done d;
            memcpy(&d, payload, sizeof(d));
            s_ieee_done_scan_id = d.scan_id;
            Serial.printf("[c5link] IEEE scan id=%u done detections=%u elapsed=%ums status=%u\n",
                          (unsigned)d.scan_id, d.detection_count, d.duration_ms, d.status);
        }
        break;

    case LINK_MSG_I2C_READ_RESP:
        if (len == sizeof(struct link_i2c_read_resp)) {
            struct link_i2c_read_resp r;
            memcpy(&r, payload, sizeof(r));
            if (s_exp_expected_type == LINK_MSG_I2C_READ_RESP &&
                r.request_id == s_exp_request_id) {
                memcpy(&s_exp_i2c_read_resp, &r, sizeof(r));
                xSemaphoreGive(s_exp_done);
            }
        }
        break;

    case LINK_MSG_I2C_WRITE_RESP:
        if (len == sizeof(struct link_i2c_write_resp)) {
            struct link_i2c_write_resp r;
            memcpy(&r, payload, sizeof(r));
            if (s_exp_expected_type == LINK_MSG_I2C_WRITE_RESP &&
                r.request_id == s_exp_request_id) {
                memcpy(&s_exp_i2c_write_resp, &r, sizeof(r));
                xSemaphoreGive(s_exp_done);
            }
        }
        break;

    case LINK_MSG_GPIO_RESP:
        if (len == sizeof(struct link_gpio_resp)) {
            struct link_gpio_resp r;
            memcpy(&r, payload, sizeof(r));
            if (s_exp_expected_type == LINK_MSG_GPIO_RESP &&
                r.request_id == s_exp_request_id) {
                memcpy(&s_exp_gpio_resp, &r, sizeof(r));
                xSemaphoreGive(s_exp_done);
            }
        }
        break;

    case LINK_MSG_STATUS:
        if (len == sizeof(struct link_status_payload)) {
            struct link_status_payload p;
            memcpy(&p, payload, sizeof(p));
            Serial.printf("[c5link] peer STATUS uptime=%u heap=%u fw=0x%04x "
                          "ok=%u err=%u\n",
                          (unsigned)p.uptime_ms, (unsigned)p.free_heap,
                          p.fw_version, p.rx_frame_ok, p.rx_frame_err);
        }
        break;

    default:
        Serial.printf("[c5link] unknown type=0x%02x seq=%u len=%u\n",
                      type, seq, (unsigned)len);
        break;
    }
}

void link_task(void * /*arg*/) {
    uint8_t rx[256];
    for (;;) {
        int avail = s_uart.available();
        if (avail > 0) {
            int to_read = avail > (int)sizeof(rx) ? (int)sizeof(rx) : avail;
            int got = s_uart.readBytes(rx, to_read);
            if (got > 0) {
                link_decoder_feed(&s_decoder, rx, (size_t)got, on_frame, nullptr);
            }
        }
        uint32_t now = (uint32_t)millis();
        if (now - s_last_ping_ms >= C5_LINK_PING_MS) {
            c5LinkSendPing();
            s_last_ping_ms = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

extern "C" {

void c5LinkInit(void) {
    if (s_initialized) {
        return;
    }
    s_tx_lock = xSemaphoreCreateMutex();
    s_wifi_queue = xQueueCreate(C5_LINK_WIFI_QUEUE_LEN, sizeof(C5WifiAp));
    s_ble_queue  = xQueueCreate(C5_LINK_BLE_QUEUE_LEN,  sizeof(C5BleAdv));
    s_ieee_queue = xQueueCreate(C5_LINK_IEEE_QUEUE_LEN, sizeof(C5Ieee802154Detection));
    s_exp_lock = xSemaphoreCreateMutex();
    s_exp_done = xSemaphoreCreateBinary();
    link_decoder_init(&s_decoder);

    s_uart.setRxBufferSize(1024);
    s_uart.begin(C5_LINK_BAUD, SERIAL_8N1, C5_LINK_RX_PIN, C5_LINK_TX_PIN);

    s_initialized = true;
    s_last_ping_ms = (uint32_t)millis();

    xTaskCreatePinnedToCore(link_task, "c5link", 4096, nullptr, 2, nullptr, 1);

    Serial.printf("[c5link] init UART%d tx=%d rx=%d baud=%u\n",
                  C5_LINK_UART_NUM,
                  (int)C5_LINK_TX_PIN, (int)C5_LINK_RX_PIN,
                  (unsigned)C5_LINK_BAUD);
}

bool c5LinkWifiScanStart(uint32_t scan_id,
                         const uint8_t *channels, uint8_t count,
                         uint16_t duration_ms, bool passive) {
    if (!s_initialized) return false;
    if (count == 0 || channels == nullptr) return false;
    if (count > LINK_WIFI_MAX_CHANNELS) count = LINK_WIFI_MAX_CHANNELS;

    struct link_wifi_scan_req req;
    memset(&req, 0, sizeof(req));
    req.scan_id       = scan_id;
    req.duration_ms   = duration_ms;
    req.passive       = passive ? 1 : 0;
    req.channel_count = count;
    memcpy(req.channels, channels, count);

    send_frame(LINK_MSG_WIFI_SCAN_REQ, s_seq++,
               reinterpret_cast<const uint8_t *>(&req), sizeof(req));
    return true;
}

bool c5LinkWifiDrainResult(struct C5WifiAp *out) {
    if (!s_wifi_queue || !out) return false;
    return xQueueReceive(s_wifi_queue, out, 0) == pdTRUE;
}

uint32_t c5LinkWifiTakeDoneScanId(void) {
    // Read + clear atomically as far as a uint32 store can be.
    uint32_t id = s_wifi_done_scan_id;
    if (id != 0) s_wifi_done_scan_id = 0;
    return id;
}

bool c5LinkBleScanStart(uint32_t scan_id, uint16_t duration_ms,
                        uint8_t phy_mask, bool active,
                        uint16_t interval_ms, uint16_t window_ms) {
    if (!s_initialized) return false;

    struct link_ble_scan_req req;
    memset(&req, 0, sizeof(req));
    req.scan_id     = scan_id;
    req.duration_ms = duration_ms;
    req.phy_mask    = phy_mask;
    req.active      = active ? 1 : 0;
    req.interval_ms = interval_ms;
    req.window_ms   = window_ms;

    send_frame(LINK_MSG_BLE_SCAN_REQ, s_seq++,
               reinterpret_cast<const uint8_t *>(&req), sizeof(req));
    return true;
}

bool c5LinkBleDrainResult(struct C5BleAdv *out) {
    if (!s_ble_queue || !out) return false;
    return xQueueReceive(s_ble_queue, out, 0) == pdTRUE;
}

uint32_t c5LinkBleTakeDoneScanId(void) {
    uint32_t id = s_ble_done_scan_id;
    if (id != 0) s_ble_done_scan_id = 0;
    return id;
}

bool c5LinkIeeeScanStart(uint32_t scan_id,
                         const uint8_t *channels, uint8_t count,
                         uint16_t duration_ms) {
    if (!s_initialized) return false;
    if (count == 0 || channels == nullptr) return false;
    if (count > LINK_IEEE_CHANNEL_COUNT_MAX) count = LINK_IEEE_CHANNEL_COUNT_MAX;

    struct link_ieee_scan_req req;
    memset(&req, 0, sizeof(req));
    req.scan_id       = scan_id;
    req.duration_ms   = duration_ms;
    req.channel_count = count;
    memcpy(req.channels, channels, count);

    send_frame(LINK_MSG_IEEE_SCAN_REQ, s_seq++,
               reinterpret_cast<const uint8_t *>(&req), sizeof(req));
    return true;
}

bool c5LinkIeeeDrainResult(struct C5Ieee802154Detection *out) {
    if (!s_ieee_queue || !out) return false;
    return xQueueReceive(s_ieee_queue, out, 0) == pdTRUE;
}

uint32_t c5LinkIeeeTakeDoneScanId(void) {
    uint32_t id = s_ieee_done_scan_id;
    if (id != 0) s_ieee_done_scan_id = 0;
    return id;
}

// ── Expansion bus: synchronous request/response (stage 7) ──────────────────

// Internal helper: serialise, set up the pending slot, send the request,
// wait for the matching response or timeout. Returns true on success
// (response landed); false on timeout or link-not-ready. Caller reads
// the appropriate s_exp_*_resp slot after success.
static bool expExchange(uint8_t req_type, uint8_t resp_type,
                        const uint8_t *req_bytes, size_t req_len,
                        uint32_t request_id, uint32_t timeout_ms) {
    if (!s_initialized || s_exp_lock == nullptr || s_exp_done == nullptr) {
        return false;
    }
    if (xSemaphoreTake(s_exp_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return false;
    }

    // Drain any stale "done" signal from a previous cancelled wait.
    xSemaphoreTake(s_exp_done, 0);

    s_exp_request_id    = request_id;
    s_exp_expected_type = resp_type;

    send_frame(req_type, s_seq++, req_bytes, req_len);

    bool ok = (xSemaphoreTake(s_exp_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);

    s_exp_expected_type = 0;
    s_exp_request_id    = 0;
    xSemaphoreGive(s_exp_lock);
    return ok;
}

static uint32_t next_request_id() {
    static uint32_t s_next = 1;
    return s_next++;
}

int c5LinkI2cRead(uint8_t addr, int reg_addr, uint8_t *buf, uint8_t len,
                  uint32_t timeout_ms) {
    if (buf == nullptr || len == 0 || len > LINK_I2C_DATA_MAX) {
        return /*BAD_PARAM*/ 4;
    }
    struct link_i2c_read_req req;
    memset(&req, 0, sizeof(req));
    req.request_id  = next_request_id();
    req.bus         = 0;
    req.device_addr = addr;
    if (reg_addr >= 0 && reg_addr <= 0xFF) {
        req.reg         = (uint8_t)reg_addr;
        req.reg_present = 1;
    }
    req.read_len = len;

    if (!expExchange(LINK_MSG_I2C_READ_REQ, LINK_MSG_I2C_READ_RESP,
                     (const uint8_t *)&req, sizeof(req), req.request_id, timeout_ms)) {
        return /*TIMEOUT*/ 2;
    }
    if (s_exp_i2c_read_resp.status == 0 /*OK*/) {
        uint8_t copy = s_exp_i2c_read_resp.read_len > len ? len : s_exp_i2c_read_resp.read_len;
        memcpy(buf, s_exp_i2c_read_resp.data, copy);
    }
    return s_exp_i2c_read_resp.status;
}

int c5LinkI2cWrite(uint8_t addr, int reg_addr,
                   const uint8_t *data, uint8_t len, uint32_t timeout_ms) {
    if (len > LINK_I2C_DATA_MAX) {
        return /*BAD_PARAM*/ 4;
    }
    if (len > 0 && data == nullptr) {
        return /*BAD_PARAM*/ 4;
    }
    struct link_i2c_write_req req;
    memset(&req, 0, sizeof(req));
    req.request_id  = next_request_id();
    req.bus         = 0;
    req.device_addr = addr;
    if (reg_addr >= 0 && reg_addr <= 0xFF) {
        req.reg         = (uint8_t)reg_addr;
        req.reg_present = 1;
    }
    req.data_len = len;
    if (len > 0) memcpy(req.data, data, len);

    if (!expExchange(LINK_MSG_I2C_WRITE_REQ, LINK_MSG_I2C_WRITE_RESP,
                     (const uint8_t *)&req, sizeof(req), req.request_id, timeout_ms)) {
        return /*TIMEOUT*/ 2;
    }
    return s_exp_i2c_write_resp.status;
}

static int gpioOp(uint8_t pin_index, uint8_t op, uint8_t mode, uint8_t value,
                  uint8_t *out_value, uint32_t timeout_ms) {
    if (pin_index >= LINK_EXP_GPIO_COUNT) return /*BAD_PARAM*/ 4;
    struct link_gpio_req req;
    memset(&req, 0, sizeof(req));
    req.request_id = next_request_id();
    req.pin_index  = pin_index;
    req.op         = op;
    req.mode       = mode;
    req.value      = value;

    if (!expExchange(LINK_MSG_GPIO_REQ, LINK_MSG_GPIO_RESP,
                     (const uint8_t *)&req, sizeof(req), req.request_id, timeout_ms)) {
        return /*TIMEOUT*/ 2;
    }
    if (out_value) *out_value = s_exp_gpio_resp.value;
    return s_exp_gpio_resp.status;
}

int c5LinkGpioConfig(uint8_t pin_index, uint8_t mode, uint32_t timeout_ms) {
    return gpioOp(pin_index, LINK_GPIO_OP_CONFIG, mode, 0, nullptr, timeout_ms);
}

int c5LinkGpioWrite(uint8_t pin_index, bool value, uint32_t timeout_ms) {
    return gpioOp(pin_index, LINK_GPIO_OP_WRITE, 0, value ? 1 : 0, nullptr, timeout_ms);
}

int c5LinkGpioRead(uint8_t pin_index, uint8_t *out_value, uint32_t timeout_ms) {
    if (out_value == nullptr) return /*BAD_PARAM*/ 4;
    return gpioOp(pin_index, LINK_GPIO_OP_READ, 0, 0, out_value, timeout_ms);
}

void c5LinkSendPing(void) {
    struct link_ping_payload p;
    p.uptime_ms = (uint32_t)millis();
    send_frame(LINK_MSG_PING, s_seq++, (const uint8_t *)&p, sizeof(p));
}

void c5LinkSendStatus(void) {
    uint32_t errs = s_decoder.stats.bad_crc + s_decoder.stats.bad_length +
                    s_decoder.stats.short_frame + s_decoder.stats.overflow;
    struct link_status_payload p;
    p.uptime_ms    = (uint32_t)millis();
    p.free_heap    = (uint32_t)ESP.getFreeHeap();
    p.fw_version   = C5_LINK_FW_VERSION;
    p.rx_frame_ok  = (uint16_t)(s_decoder.stats.ok & 0xFFFFu);
    p.rx_frame_err = (uint16_t)(errs & 0xFFFFu);
    p.reserved     = 0;
    send_frame(LINK_MSG_STATUS, s_seq++, (const uint8_t *)&p, sizeof(p));
}

void c5LinkLogStats(void) {
    Serial.printf("[c5link] stats ok=%u crc=%u len=%u short=%u ovf=%u\n",
                  (unsigned)s_decoder.stats.ok,
                  (unsigned)s_decoder.stats.bad_crc,
                  (unsigned)s_decoder.stats.bad_length,
                  (unsigned)s_decoder.stats.short_frame,
                  (unsigned)s_decoder.stats.overflow);
}

}  // extern "C"
