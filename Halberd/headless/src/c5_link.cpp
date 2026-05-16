#include "c5_link.h"

#include <Arduino.h>
#include <FS.h>
#include <HardwareSerial.h>
#include <SD.h>
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
// Probe events come in fast — a single phone in a busy environment can
// burst dozens per second across both bands. 256 matches the S3-side
// probeRequestQueue depth; each entry is ~158 B so ~40 KB.
constexpr size_t   C5_LINK_WIFI_PROBE_QUEUE_LEN = 256;

HardwareSerial    s_uart(C5_LINK_UART_NUM);
link_decoder_t    s_decoder;
SemaphoreHandle_t s_tx_lock = nullptr;
uint8_t           s_seq = 0;
bool              s_initialized = false;
uint32_t          s_last_ping_ms = 0;
QueueHandle_t     s_wifi_queue = nullptr;
QueueHandle_t     s_ble_queue = nullptr;
QueueHandle_t     s_ieee_queue = nullptr;
QueueHandle_t     s_wifi_probe_queue = nullptr;
volatile uint32_t s_wifi_done_scan_id = 0;
volatile uint32_t s_ble_done_scan_id = 0;
volatile uint32_t s_ieee_done_scan_id = 0;
volatile uint32_t s_wifi_probe_done_scan_id = 0;

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

// ── SD-proxy server state (stage 9) ───────────────────────────────────────
//
// Mirrors Halberd/full/src/c5_link.cpp — see comments there. The SD-card
// slot is wired to the S3 in both variants, so the same SD-proxy server
// ships in both. Worker task offloads SD-card latency from the link RX
// path; a small LRU cache keeps fs::File handles open across back-to-back
// chunk writes.

constexpr size_t   C5_LINK_SD_QUEUE_LEN     = 4;
constexpr size_t   C5_LINK_SD_HANDLE_CACHE  = 4;
constexpr uint32_t C5_LINK_SD_IDLE_CLOSE_MS = 1000;

enum sd_op_kind : uint8_t {
    SD_OP_READ  = 1,
    SD_OP_WRITE = 2,
    SD_OP_STAT  = 3,
};

struct sd_op_t {
    uint8_t kind;
    union {
        struct link_sd_read_req  read_req;
        struct link_sd_write_req write_req;
        struct link_sd_stat_req  stat_req;
    };
};

struct sd_cache_slot {
    bool     used;
    bool     write_mode;
    char     path[LINK_SD_PATH_MAX + 1];
    fs::File file;
    uint32_t last_used_ms;
    uint32_t bytes_written;
};

QueueHandle_t s_sd_queue = nullptr;
sd_cache_slot s_sd_cache[C5_LINK_SD_HANDLE_CACHE];

c5LinkSensorEventCb s_sensor_event_cb = nullptr;

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

// ── SD-proxy server helpers ───────────────────────────────────────────────
// Mirrors Halberd/full/src/c5_link.cpp. See that file for design notes.

bool sd_path_allowed(const char *path, uint8_t plen) {
    if (path == nullptr || plen == 0 || plen > LINK_SD_PATH_MAX) {
        return false;
    }
    if (path[0] != '/') return false;
    for (uint8_t i = 0; i + 1 < plen; i++) {
        if (path[i] == '.' && path[i + 1] == '.') return false;
    }
    if (plen == 14 && memcmp(path, "/sensors.json", 13) == 0 && path[13] != '/') {
        return true;
    }
    auto starts_with = [&](const char *prefix, uint8_t prefix_len) -> bool {
        return plen >= prefix_len && memcmp(path, prefix, prefix_len) == 0;
    };
    if (starts_with("/sensors/",    9)) return true;
    if (starts_with("/sensorlogs/", 12)) return true;
    if (starts_with("/captures/",   10)) return true;
    return false;
}

void sd_path_to_cstr(const char *path, uint8_t plen, char out[LINK_SD_PATH_MAX + 1]) {
    if (plen > LINK_SD_PATH_MAX) plen = LINK_SD_PATH_MAX;
    memcpy(out, path, plen);
    out[plen] = '\0';
}

void sd_cache_evict_idle() {
    const uint32_t now = (uint32_t)millis();
    for (auto &slot : s_sd_cache) {
        if (slot.used && (now - slot.last_used_ms) >= C5_LINK_SD_IDLE_CLOSE_MS) {
            if (slot.file) slot.file.close();
            slot.used = false;
            slot.bytes_written = 0;
            slot.path[0] = '\0';
        }
    }
}

sd_cache_slot *sd_cache_find(const char *path, bool write_mode) {
    for (auto &slot : s_sd_cache) {
        if (slot.used && slot.write_mode == write_mode &&
            strncmp(slot.path, path, LINK_SD_PATH_MAX) == 0) {
            slot.last_used_ms = (uint32_t)millis();
            return &slot;
        }
    }
    return nullptr;
}

sd_cache_slot *sd_cache_alloc(const char *path, bool write_mode) {
    sd_cache_slot *lru = &s_sd_cache[0];
    for (auto &slot : s_sd_cache) {
        if (!slot.used) { lru = &slot; break; }
        if (slot.last_used_ms < lru->last_used_ms) lru = &slot;
    }
    if (lru->used && lru->file) lru->file.close();
    lru->used         = true;
    lru->write_mode   = write_mode;
    lru->last_used_ms = (uint32_t)millis();
    lru->bytes_written = 0;
    strncpy(lru->path, path, LINK_SD_PATH_MAX);
    lru->path[LINK_SD_PATH_MAX] = '\0';
    return lru;
}

void sd_cache_close(sd_cache_slot *slot) {
    if (!slot) return;
    if (slot->file) slot->file.close();
    slot->used = false;
    slot->bytes_written = 0;
    slot->path[0] = '\0';
}

void sd_handle_read(const struct link_sd_read_req &req) {
    struct link_sd_read_resp resp = {};
    resp.request_id = req.request_id;
    if (!sd_path_allowed(req.path, req.path_len)) {
        resp.status = LINK_SD_STATUS_DENIED;
        send_frame(LINK_MSG_SD_READ_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    if (!SafeSD::isAvailable()) {
        resp.status = LINK_SD_STATUS_NO_SD;
        send_frame(LINK_MSG_SD_READ_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    char path_cstr[LINK_SD_PATH_MAX + 1];
    sd_path_to_cstr(req.path, req.path_len, path_cstr);

    sd_cache_slot *slot = sd_cache_find(path_cstr, false);
    if (!slot) {
        if (!SafeSD::exists(path_cstr)) {
            resp.status = LINK_SD_STATUS_NOT_FOUND;
            send_frame(LINK_MSG_SD_READ_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
            return;
        }
        slot = sd_cache_alloc(path_cstr, false);
        slot->file = SafeSD::open(path_cstr, FILE_READ);
        if (!slot->file) {
            sd_cache_close(slot);
            resp.status = LINK_SD_STATUS_IO_ERROR;
            send_frame(LINK_MSG_SD_READ_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
            return;
        }
    }
    if (!slot->file.seek(req.offset)) {
        resp.status = LINK_SD_STATUS_IO_ERROR;
        send_frame(LINK_MSG_SD_READ_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    uint16_t want = req.read_len > LINK_SD_DATA_MAX ? LINK_SD_DATA_MAX : req.read_len;
    int got = SafeSD::read(slot->file, resp.data, want);
    if (got < 0) {
        resp.status = LINK_SD_STATUS_IO_ERROR;
        send_frame(LINK_MSG_SD_READ_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    resp.status   = LINK_SD_STATUS_OK;
    resp.data_len = (uint16_t)got;
    resp.eof      = (got < want || (uint32_t)slot->file.position() >= (uint32_t)slot->file.size()) ? 1 : 0;
    send_frame(LINK_MSG_SD_READ_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
}

void sd_handle_write(const struct link_sd_write_req &req) {
    struct link_sd_write_resp resp = {};
    resp.request_id = req.request_id;
    if (!sd_path_allowed(req.path, req.path_len)) {
        resp.status = LINK_SD_STATUS_DENIED;
        send_frame(LINK_MSG_SD_WRITE_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    if (!SafeSD::isAvailable()) {
        resp.status = LINK_SD_STATUS_NO_SD;
        send_frame(LINK_MSG_SD_WRITE_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    char path_cstr[LINK_SD_PATH_MAX + 1];
    sd_path_to_cstr(req.path, req.path_len, path_cstr);

    sd_cache_slot *slot = sd_cache_find(path_cstr, true);
    const bool first_chunk = (req.flags & LINK_SD_WRITE_FLAG_CREATE_TRUNCATE) != 0;
    if (slot && first_chunk) {
        sd_cache_close(slot);
        slot = nullptr;
    }
    if (!slot) {
        slot = sd_cache_alloc(path_cstr, true);
        const char *mode = first_chunk ? FILE_WRITE : FILE_APPEND;
        slot->file = SafeSD::open(path_cstr, mode);
        if (!slot->file) {
            sd_cache_close(slot);
            resp.status = LINK_SD_STATUS_IO_ERROR;
            send_frame(LINK_MSG_SD_WRITE_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
            return;
        }
    }
    if (req.data_len > 0) {
        size_t wrote = SafeSD::write(slot->file, req.data, req.data_len);
        if (wrote != req.data_len) {
            sd_cache_close(slot);
            resp.status = LINK_SD_STATUS_IO_ERROR;
            send_frame(LINK_MSG_SD_WRITE_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
            return;
        }
        slot->bytes_written += wrote;
    }
    if (req.flags & LINK_SD_WRITE_FLAG_FINAL_CHUNK) {
        SafeSD::flush(slot->file);
        const uint32_t final_bytes = slot->bytes_written;
        sd_cache_close(slot);
        resp.status        = LINK_SD_STATUS_OK;
        resp.bytes_written = final_bytes;
    } else {
        resp.status        = LINK_SD_STATUS_OK;
        resp.bytes_written = slot->bytes_written;
    }
    send_frame(LINK_MSG_SD_WRITE_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
}

void sd_handle_stat(const struct link_sd_stat_req &req) {
    struct link_sd_stat_resp resp = {};
    resp.request_id = req.request_id;
    if (!sd_path_allowed(req.path, req.path_len)) {
        resp.status = LINK_SD_STATUS_DENIED;
        send_frame(LINK_MSG_SD_STAT_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    if (!SafeSD::isAvailable()) {
        resp.status = LINK_SD_STATUS_NO_SD;
        send_frame(LINK_MSG_SD_STAT_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    char path_cstr[LINK_SD_PATH_MAX + 1];
    sd_path_to_cstr(req.path, req.path_len, path_cstr);
    if (!SafeSD::exists(path_cstr)) {
        resp.status = LINK_SD_STATUS_NOT_FOUND;
        send_frame(LINK_MSG_SD_STAT_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    fs::File f = SafeSD::open(path_cstr, FILE_READ);
    if (!f) {
        resp.status = LINK_SD_STATUS_IO_ERROR;
        send_frame(LINK_MSG_SD_STAT_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
        return;
    }
    resp.status     = LINK_SD_STATUS_OK;
    resp.is_dir     = f.isDirectory() ? 1 : 0;
    resp.size_bytes = (uint32_t)f.size();
    resp.mtime_unix = 0;
    f.close();
    send_frame(LINK_MSG_SD_STAT_RESP, s_seq++, (const uint8_t *)&resp, sizeof(resp));
}

void sd_proxy_task(void * /*arg*/) {
    sd_op_t op;
    for (;;) {
        if (xQueueReceive(s_sd_queue, &op, portMAX_DELAY) != pdTRUE) continue;
        sd_cache_evict_idle();
        switch (op.kind) {
        case SD_OP_READ:  sd_handle_read(op.read_req);   break;
        case SD_OP_WRITE: sd_handle_write(op.write_req); break;
        case SD_OP_STAT:  sd_handle_stat(op.stat_req);   break;
        default: break;
        }
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

    case LINK_MSG_WIFI_PROBE_EVENT:
        if (len == sizeof(struct link_wifi_probe_event) && s_wifi_probe_queue != nullptr) {
            struct link_wifi_probe_event w;
            memcpy(&w, payload, sizeof(w));
            C5WifiProbeEvent ev;
            ev.scan_id     = w.scan_id;
            ev.rssi        = w.rssi;
            ev.channel     = w.channel;
            ev.is_response = w.is_response;
            memcpy(ev.src_mac, w.src_mac, 6);
            memcpy(ev.dst_mac, w.dst_mac, 6);
            memcpy(ev.bssid,   w.bssid,   6);
            ev.payload_len = w.payload_len > C5_WIFI_PROBE_PAYLOAD_MAX
                                 ? C5_WIFI_PROBE_PAYLOAD_MAX
                                 : w.payload_len;
            memcpy(ev.payload, w.payload, C5_WIFI_PROBE_PAYLOAD_MAX);
            if (xQueueSend(s_wifi_probe_queue, &ev, 0) != pdTRUE) {
                C5WifiProbeEvent drop;
                xQueueReceive(s_wifi_probe_queue, &drop, 0);
                xQueueSend(s_wifi_probe_queue, &ev, 0);
            }
        }
        break;

    case LINK_MSG_WIFI_PROBE_DONE:
        if (len == sizeof(struct link_wifi_probe_done)) {
            struct link_wifi_probe_done d;
            memcpy(&d, payload, sizeof(d));
            s_wifi_probe_done_scan_id = d.scan_id;
            Serial.printf("[c5link] WIFI probe id=%u done events=%u elapsed=%ums status=%u\n",
                          (unsigned)d.scan_id, d.event_count, d.duration_ms, d.status);
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

    case LINK_MSG_SENSOR_EVENT:
        if (len == sizeof(struct link_sensor_event) && s_sensor_event_cb != nullptr) {
            struct link_sensor_event ev;
            memcpy(&ev, payload, sizeof(ev));
            s_sensor_event_cb(&ev);
        }
        break;

    case LINK_MSG_SD_READ_REQ:
        if (len == sizeof(struct link_sd_read_req) && s_sd_queue != nullptr) {
            sd_op_t op;
            op.kind = SD_OP_READ;
            memcpy(&op.read_req, payload, sizeof(op.read_req));
            if (xQueueSend(s_sd_queue, &op, 0) != pdTRUE) {
                struct link_sd_read_resp r = {};
                r.request_id = op.read_req.request_id;
                r.status     = LINK_SD_STATUS_BUSY;
                send_frame(LINK_MSG_SD_READ_RESP, s_seq++,
                           (const uint8_t *)&r, sizeof(r));
            }
        }
        break;

    case LINK_MSG_SD_WRITE_REQ:
        if (len == sizeof(struct link_sd_write_req) && s_sd_queue != nullptr) {
            sd_op_t op;
            op.kind = SD_OP_WRITE;
            memcpy(&op.write_req, payload, sizeof(op.write_req));
            if (xQueueSend(s_sd_queue, &op, 0) != pdTRUE) {
                struct link_sd_write_resp r = {};
                r.request_id = op.write_req.request_id;
                r.status     = LINK_SD_STATUS_BUSY;
                send_frame(LINK_MSG_SD_WRITE_RESP, s_seq++,
                           (const uint8_t *)&r, sizeof(r));
            }
        }
        break;

    case LINK_MSG_SD_STAT_REQ:
        if (len == sizeof(struct link_sd_stat_req) && s_sd_queue != nullptr) {
            sd_op_t op;
            op.kind = SD_OP_STAT;
            memcpy(&op.stat_req, payload, sizeof(op.stat_req));
            if (xQueueSend(s_sd_queue, &op, 0) != pdTRUE) {
                struct link_sd_stat_resp r = {};
                r.request_id = op.stat_req.request_id;
                r.status     = LINK_SD_STATUS_BUSY;
                send_frame(LINK_MSG_SD_STAT_RESP, s_seq++,
                           (const uint8_t *)&r, sizeof(r));
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
    s_wifi_queue       = xQueueCreate(C5_LINK_WIFI_QUEUE_LEN,        sizeof(C5WifiAp));
    s_ble_queue        = xQueueCreate(C5_LINK_BLE_QUEUE_LEN,         sizeof(C5BleAdv));
    s_ieee_queue       = xQueueCreate(C5_LINK_IEEE_QUEUE_LEN,        sizeof(C5Ieee802154Detection));
    s_wifi_probe_queue = xQueueCreate(C5_LINK_WIFI_PROBE_QUEUE_LEN,  sizeof(C5WifiProbeEvent));
    s_exp_lock = xSemaphoreCreateMutex();
    s_exp_done = xSemaphoreCreateBinary();
    s_sd_queue = xQueueCreate(C5_LINK_SD_QUEUE_LEN, sizeof(sd_op_t));
    for (auto &slot : s_sd_cache) {
        slot.used         = false;
        slot.write_mode   = false;
        slot.path[0]      = '\0';
        slot.last_used_ms = 0;
        slot.bytes_written = 0;
    }
    link_decoder_init(&s_decoder);

    s_uart.setRxBufferSize(1024);
    s_uart.begin(C5_LINK_BAUD, SERIAL_8N1, C5_LINK_RX_PIN, C5_LINK_TX_PIN);

    s_initialized = true;
    s_last_ping_ms = (uint32_t)millis();

    xTaskCreatePinnedToCore(link_task, "c5link", 4096, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(sd_proxy_task, "c5sdproxy", 4096, nullptr, 2, nullptr, 1);

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

bool c5LinkWifiProbeSniffStart(uint32_t scan_id,
                               const uint8_t *channels, uint8_t count,
                               uint16_t duration_ms, bool capture_responses) {
    if (!s_initialized) return false;
    if (count == 0 || channels == nullptr) return false;
    if (count > LINK_WIFI_MAX_CHANNELS) count = LINK_WIFI_MAX_CHANNELS;

    struct link_wifi_probe_req req;
    memset(&req, 0, sizeof(req));
    req.scan_id           = scan_id;
    req.duration_ms       = duration_ms;
    req.capture_responses = capture_responses ? 1 : 0;
    req.channel_count     = count;
    memcpy(req.channels, channels, count);

    send_frame(LINK_MSG_WIFI_PROBE_REQ, s_seq++,
               reinterpret_cast<const uint8_t *>(&req), sizeof(req));
    return true;
}

bool c5LinkWifiProbeDrainResult(struct C5WifiProbeEvent *out) {
    if (!s_wifi_probe_queue || !out) return false;
    return xQueueReceive(s_wifi_probe_queue, out, 0) == pdTRUE;
}

uint32_t c5LinkWifiProbeTakeDoneScanId(void) {
    uint32_t id = s_wifi_probe_done_scan_id;
    if (id != 0) s_wifi_probe_done_scan_id = 0;
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

void c5LinkRegisterSensorEventCallback(c5LinkSensorEventCb cb) {
    s_sensor_event_cb = cb;
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
