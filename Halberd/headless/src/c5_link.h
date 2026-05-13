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

// ── BLE scan API (stage 5) ─────────────────────────────────────────────────
// One BLE advertising event drained from the C5's scan stream. Fields
// mirror link_ble_adv in Halberd/shared/link_protocol.h.
#define C5_BLE_ADV_DATA_MAX 62

struct C5BleAdv {
    uint32_t scan_id;
    uint8_t  addr[6];
    uint8_t  addr_type;       // 0=public, 1=random, 2=public-id, 3=random-id
    int8_t   rssi;
    uint8_t  primary_phy;     // 1=1M, 3=Coded, 0=unknown
    uint8_t  secondary_phy;   // 0=none, 1=1M, 2=2M, 3=Coded
    int8_t   tx_power;        // dBm, INT8_MIN if unknown
    uint8_t  adv_type;        // BLE_HCI_ADV_RPT_EVTYPE_* / extended adv props
    uint8_t  adv_data_len;
    uint8_t  adv_data[C5_BLE_ADV_DATA_MAX];
};

// Issue a BLE scan on the C5. Non-blocking; results stream back via the
// adv queue and are drained with c5LinkBleDrainResult().
//
// phy_mask uses link_ble_phy_mask bits (1=1M, 2=2M, 4=Coded). Pass 0x05
// (1M + Coded) for the typical "everything the S3 can't see" scan.
// active=true requests scan responses; false is passive (faster, less
// info). interval_ms and window_ms may be 0 to use NimBLE defaults.
bool c5LinkBleScanStart(uint32_t scan_id, uint16_t duration_ms,
                        uint8_t phy_mask, bool active,
                        uint16_t interval_ms, uint16_t window_ms);

// Drain one buffered BLE advertising event. Returns true and fills *out
// if a record was available, false if the queue is empty.
bool c5LinkBleDrainResult(struct C5BleAdv *out);

// Most recent BLE_SCAN_DONE's scan_id (0 if none); clears on read.
uint32_t c5LinkBleTakeDoneScanId(void);

#ifdef __cplusplus
}
#endif
