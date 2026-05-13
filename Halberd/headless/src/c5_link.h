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

// ── IEEE 802.15.4 sniffer API (stage 6) ────────────────────────────────────
// One parsed 802.15.4 detection drained from the C5. Mirrors
// link_ieee_detection in Halberd/shared/link_protocol.h.
#define C5_IEEE_PAYLOAD_MAX  64
#define C5_IEEE_ADDR_LEN      8

struct C5Ieee802154Detection {
    uint32_t scan_id;
    uint8_t  channel;            // 11..26
    int8_t   rssi;
    uint8_t  lqi;
    uint8_t  frame_type;         // 0=beacon, 1=data, 2=ack, 3=cmd
    uint8_t  frame_version;      // 0=2003, 1=2006, 2=2015
    uint8_t  protocol_family;    // 0=Unknown, 1=Zigbee, 2=Thread, 3=Matter, 99=Other
    uint8_t  seq_num;
    uint8_t  flags;              // bit0=security, bit1=ack_req, bit2=frame_pending, …
    uint16_t dst_pan;            // 0xFFFF when absent or broadcast
    uint16_t src_pan;            // 0xFFFF when absent / PAN ID compression
    uint8_t  dst_addr_mode;      // 0=none, 2=short, 3=extended
    uint8_t  dst_addr[C5_IEEE_ADDR_LEN];
    uint8_t  src_addr_mode;
    uint8_t  src_addr[C5_IEEE_ADDR_LEN];
    uint8_t  payload_len;
    uint8_t  payload[C5_IEEE_PAYLOAD_MAX];
};

// Trigger an 802.15.4 scan. channels[count] are channel numbers 11..26.
// duration_ms is divided across channels by the C5.
bool c5LinkIeeeScanStart(uint32_t scan_id,
                         const uint8_t *channels, uint8_t count,
                         uint16_t duration_ms);

bool c5LinkIeeeDrainResult(struct C5Ieee802154Detection *out);
uint32_t c5LinkIeeeTakeDoneScanId(void);

// ── Expansion bus API (stage 7) ────────────────────────────────────────────
// Synchronous I²C + GPIO ops against the C5's expansion bus. Each call
// blocks until the C5 responds or the timeout elapses; only one
// expansion op may be in flight at a time (the implementation serialises).
//
// Return values: status enum (0=OK, 1=NACK, 2=TIMEOUT, 3=BUSY, 4=BAD_PARAM,
// 5=NOT_READY). Synonymous with link_exp_status on the wire.

// Read `len` bytes from device `addr`. If `reg_addr` is non-negative,
// the C5 writes that register byte before the repeated-start read.
// Pass -1 to skip the register write.
// Returns the wire status code; 0 = OK and `buf` is populated.
int c5LinkI2cRead(uint8_t addr, int reg_addr, uint8_t *buf, uint8_t len,
                  uint32_t timeout_ms);

// Write `len` bytes (optionally prefixed with a register byte) to device
// `addr`. Pass reg_addr = -1 for raw writes.
int c5LinkI2cWrite(uint8_t addr, int reg_addr,
                   const uint8_t *data, uint8_t len, uint32_t timeout_ms);

// Configure an EXP_GPIO pin (index 0..4 = EXP_GPIO0..4). `mode` is a
// link_gpio_mode value (0=INPUT, 1=OUTPUT, 2=INPUT_PULLUP, …).
int c5LinkGpioConfig(uint8_t pin_index, uint8_t mode, uint32_t timeout_ms);

// Set the output level on an EXP_GPIO pin previously configured as output.
int c5LinkGpioWrite(uint8_t pin_index, bool value, uint32_t timeout_ms);

// Read the input level. On success, *out_value is 0 or 1.
int c5LinkGpioRead(uint8_t pin_index, uint8_t *out_value, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
