#pragma once

// S3-side companion of the Halberd C5 link layer.
//
// Mirrors Halberd/c5/main/link.h. UART2 (GPIO43 TX / GPIO44 RX @ 921600 baud)
// is dedicated to the C5 on the v5 carrier; on v4 these same pins ran the
// GPS UART, which now lives on the C5. Stage 2 wired the codec and the
// FreeRTOS task; stage 3 made initializeGPS() call c5LinkInit() and
// rerouted GPS data through the link; stage 4 adds Wi-Fi scan mirroring.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Link bring-up + housekeeping ───────────────────────────────────────────
void c5LinkInit(void);
void c5LinkSendPing(void);
void c5LinkSendStatus(void);
void c5LinkLogStats(void);

// ── Wi-Fi scan API (stage 4) ───────────────────────────────────────────────
// One AP record drained from the C5's scan stream. Fields mirror
// link_wifi_ap_result in Halberd/shared/link_protocol.h.
struct C5WifiAp {
    uint32_t scan_id;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  bssid[6];
    uint8_t  auth_mode;       // wifi_auth_mode_t value, see Arduino-ESP32 WiFiType.h
    uint8_t  ssid_len;        // 0..32, ssid not NUL-terminated if 32
    char     ssid[32];
    uint8_t  phy_modes;       // bit0=11b, bit1=11g, bit2=11n, bit3=11ac, bit4=11ax, bit5=LR, bit6=WPS
};

// Issue a Wi-Fi scan request to the C5. Non-blocking: the C5 streams back
// AP results asynchronously; the link task pushes them into an internal
// queue. The scanner task drains via c5LinkWifiDrainResult().
//
// scan_id is caller-chosen (typical: millis() at the issue site) and is
// echoed on every result + DONE marker so callers can correlate. channels
// must hold `count` channel numbers (1..165); count is clamped to 32.
//
// Returns true if the request was framed and written to the UART, false
// if the link isn't ready or the request didn't fit.
bool c5LinkWifiScanStart(uint32_t scan_id,
                         const uint8_t *channels, uint8_t count,
                         uint16_t duration_ms, bool passive);

// Drain one buffered Wi-Fi AP result. Returns true and fills *out if a
// record was available, false if the queue is empty. Non-blocking.
bool c5LinkWifiDrainResult(struct C5WifiAp *out);

// Returns the most recent DONE marker's scan_id (or 0 if none seen yet)
// and clears it. Pattern: drain all results, then check this to learn
// whether the most recent scan finished and which one.
uint32_t c5LinkWifiTakeDoneScanId(void);

#ifdef __cplusplus
}
#endif
