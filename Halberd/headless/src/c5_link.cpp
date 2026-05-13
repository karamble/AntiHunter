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

HardwareSerial    s_uart(C5_LINK_UART_NUM);
link_decoder_t    s_decoder;
SemaphoreHandle_t s_tx_lock = nullptr;
uint8_t           s_seq = 0;
bool              s_initialized = false;
uint32_t          s_last_ping_ms = 0;
QueueHandle_t     s_wifi_queue = nullptr;
volatile uint32_t s_wifi_done_scan_id = 0;

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
