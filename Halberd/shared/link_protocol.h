#pragma once

// Halberd S3 ↔ C5 link protocol — message types and payload structs.
//
// Framing details (COBS + CRC-16) live in link_codec.h. This file is the
// single source of truth for message-type byte values and the on-the-wire
// payload layouts. It's shared verbatim between the C5 firmware and the
// Halberd S3 firmware variants (full + headless).
//
// Conventions:
//   - All multi-byte integers are little-endian (matches both ESP32 hosts).
//   - Payload structs are `__attribute__((packed))` so the sender's struct
//     bytes equal the wire bytes equal the receiver's struct bytes.
//   - Message-type bytes are grouped: 0x0x = control, 0x1x = GPS,
//     0x2x = Wi-Fi, 0x3x = BLE, 0x4x = 802.15.4, 0x5x = I²C,
//     0x6x = GPIO, 0xFx = housekeeping.

#include <stdint.h>

enum link_msg_type {
    LINK_MSG_PING            = 0x01,  // either side → other side, with peer uptime
    LINK_MSG_PONG            = 0x02,  // reply to PING, echoes the original payload
    LINK_MSG_GPS_FIX         = 0x10,  // C5 → S3, pushed at 1 Hz when NMEA flows
    LINK_MSG_WIFI_SCAN_REQ   = 0x20,  // S3 → C5, kick a Wi-Fi scan
    LINK_MSG_WIFI_AP_RESULT  = 0x21,  // C5 → S3, one per AP seen in a scan
    LINK_MSG_WIFI_SCAN_DONE  = 0x22,  // C5 → S3, end of a scan
    LINK_MSG_BLE_SCAN_REQ    = 0x30,  // S3 → C5, kick a BLE scan
    LINK_MSG_BLE_ADV         = 0x31,  // C5 → S3, one per BLE advertisement event
    LINK_MSG_BLE_SCAN_DONE   = 0x32,  // C5 → S3, end of a BLE scan
    LINK_MSG_IEEE_SCAN_REQ   = 0x40,  // S3 → C5, kick an IEEE 802.15.4 scan
    LINK_MSG_IEEE_DETECTION  = 0x41,  // C5 → S3, one per parsed 802.15.4 frame
    LINK_MSG_IEEE_SCAN_DONE  = 0x42,  // C5 → S3, end of an 802.15.4 scan
    LINK_MSG_I2C_READ_REQ    = 0x50,  // S3 → C5, read N bytes from a device
    LINK_MSG_I2C_READ_RESP   = 0x51,  // C5 → S3, result of an I2C read
    LINK_MSG_I2C_WRITE_REQ   = 0x52,  // S3 → C5, write N bytes to a device
    LINK_MSG_I2C_WRITE_RESP  = 0x53,  // C5 → S3, ack of an I2C write
    LINK_MSG_GPIO_REQ        = 0x60,  // S3 → C5, configure / write / read EXP_GPIOn
    LINK_MSG_GPIO_RESP       = 0x61,  // C5 → S3, result of a GPIO op
    LINK_MSG_STATUS          = 0xF0,  // periodic health beacon (sender-initiated)
    LINK_MSG_LOG             = 0xFE,  // forwarded log line (C5 → S3, future stage)
};

// Wi-Fi scan status codes returned in link_wifi_scan_done.status.
enum link_wifi_scan_status {
    LINK_WIFI_STATUS_OK    = 0,
    LINK_WIFI_STATUS_BUSY  = 1,
    LINK_WIFI_STATUS_ERROR = 2,
};

// BLE scan status codes returned in link_ble_scan_done.status.
enum link_ble_scan_status {
    LINK_BLE_STATUS_OK    = 0,
    LINK_BLE_STATUS_BUSY  = 1,
    LINK_BLE_STATUS_ERROR = 2,
};

// PHY mask bits used in link_ble_scan_req.phy_mask. Multiple bits may be
// set; the C5 honours whichever its NimBLE config supports. ble_gap_disc
// itself dispatches on the 1M and Coded PHYs via the extended-scan API
// when BT_NIMBLE_EXT_ADV is enabled.
enum link_ble_phy_mask {
    LINK_BLE_PHY_1M    = 0x01,  // legacy 1 Mbit/s primary
    LINK_BLE_PHY_2M    = 0x02,  // BLE 5 high-speed (secondary only — no scan on 2M)
    LINK_BLE_PHY_CODED = 0x04,  // BLE 5 long-range (S=8 / S=2)
};

// IEEE 802.15.4 scan status (link_ieee_scan_done.status).
enum link_ieee_scan_status {
    LINK_IEEE_STATUS_OK    = 0,
    LINK_IEEE_STATUS_BUSY  = 1,
    LINK_IEEE_STATUS_ERROR = 2,
};

// Protocol family classification carried in link_ieee_detection.protocol_family.
// Classification is heuristic (Stack Profile byte for Zigbee, MLE / 6LoWPAN
// signatures for Thread/Matter) — the S3 + diginode-cc can refine further.
enum link_ieee_protocol_family {
    LINK_IEEE_PROTO_UNKNOWN = 0,
    LINK_IEEE_PROTO_ZIGBEE  = 1,   // beacon Stack Profile 0x01 (2007) or 0x02 (Pro)
    LINK_IEEE_PROTO_THREAD  = 2,   // MLE Discovery / OpenThread network
    LINK_IEEE_PROTO_MATTER  = 3,   // Thread-based, additional service signatures
    LINK_IEEE_PROTO_OTHER   = 99,  // 6LoWPAN / Wireless HART / proprietary
};

// Frame-type values matching IEEE 802.15.4 FCF bits 0-2.
enum link_ieee_frame_type {
    LINK_IEEE_FRAME_BEACON     = 0,
    LINK_IEEE_FRAME_DATA       = 1,
    LINK_IEEE_FRAME_ACK        = 2,
    LINK_IEEE_FRAME_CMD        = 3,
    LINK_IEEE_FRAME_RESERVED   = 4,
    LINK_IEEE_FRAME_MULTIPURP  = 5,
    LINK_IEEE_FRAME_FRAGMENT   = 6,
    LINK_IEEE_FRAME_EXTENDED   = 7,
};

// Status codes returned in I²C / GPIO response frames.
enum link_exp_status {
    LINK_EXP_STATUS_OK         = 0,
    LINK_EXP_STATUS_NACK       = 1,  // I²C ACK never came (no device at addr)
    LINK_EXP_STATUS_TIMEOUT    = 2,
    LINK_EXP_STATUS_BUSY       = 3,
    LINK_EXP_STATUS_BAD_PARAM  = 4,  // bad addr / length / pin index
    LINK_EXP_STATUS_NOT_READY  = 5,  // bus not initialised
};

// GPIO operations carried in link_gpio_req.op.
enum link_gpio_op {
    LINK_GPIO_OP_CONFIG = 0,         // set mode for pin_index
    LINK_GPIO_OP_WRITE  = 1,         // set value on a configured-output pin
    LINK_GPIO_OP_READ   = 2,         // read a configured-input pin
};

// GPIO pin modes for LINK_GPIO_OP_CONFIG.
enum link_gpio_mode {
    LINK_GPIO_MODE_INPUT          = 0,
    LINK_GPIO_MODE_OUTPUT         = 1,
    LINK_GPIO_MODE_INPUT_PULLUP   = 2,
    LINK_GPIO_MODE_INPUT_PULLDOWN = 3,
    LINK_GPIO_MODE_OPEN_DRAIN     = 4,
};

// PING / PONG payload.  The receiver echoes this verbatim in the PONG so the
// sender can compute round-trip time as (now - uptime_ms).
struct link_ping_payload {
    uint32_t uptime_ms;
} __attribute__((packed));

// GPS_FIX payload — pushed by the C5 once per second when NMEA sentences
// arrive on UART1. All fields are sender-provided; the S3 caches the most
// recent frame and treats it as authoritative until a newer one arrives or
// a freshness timeout elapses.
//
// Angles are encoded as int32 in degrees × 1e7. Lat range fits in
// [-900_000_000, +900_000_000]; lon in [-1_800_000_000, +1_800_000_000].
// (Lat scaled is ±9e8, well within int32 range; lon ±1.8e9 fits in int32
// as well — max int32 ≈ 2.147e9.) HDOP is encoded as uint16 × 100, so 1.23
// → 123. Speed and course are × 10.
struct link_gps_fix {
    uint32_t uptime_ms;        // C5 uptime when this frame was assembled
    uint8_t  fix_valid;        // 0 = no fix, 1 = 2D/3D fix usable
    uint8_t  satellites;       // from $xxGGA
    uint16_t hdop_x100;        // HDOP × 100, e.g. 123 = 1.23
    int32_t  lat_e7;           // degrees × 1e7, north positive
    int32_t  lon_e7;           // degrees × 1e7, east positive
    int16_t  altitude_m;       // meters above mean sea level
    uint16_t speed_kmh_x10;    // km/h × 10 (knots × 1.852 × 10)
    uint16_t course_deg_x10;   // course over ground, degrees × 10
    uint16_t year;             // e.g. 2026 (UTC, from $xxRMC date)
    uint8_t  month;            // 1-12
    uint8_t  day;              // 1-31
    uint8_t  hour;             // 0-23 UTC
    uint8_t  minute;           // 0-59
    uint8_t  second;           // 0-59
    uint8_t  centisecond;      // 0-99 (10 ms units, from RMC time fraction)
    uint8_t  date_valid;       // 1 if RMC reported a usable date
    uint8_t  time_valid;       // 1 if RMC reported a usable time
    uint8_t  reserved;
} __attribute__((packed));

// ── Wi-Fi scan (stage 4) ───────────────────────────────────────────────────
//
// S3 sends WIFI_SCAN_REQ; the C5 walks the channel list, scanning each one
// for ~duration_ms / channel_count, and emits one WIFI_AP_RESULT frame per
// AP discovered, ending with WIFI_SCAN_DONE. All result frames carry the
// scan_id from the request so the S3 can correlate.

#define LINK_WIFI_MAX_CHANNELS  32
#define LINK_WIFI_SSID_MAX      32   // matches wifi_ap_record_t.ssid layout
#define LINK_WIFI_BSSID_LEN     6

struct link_wifi_scan_req {
    uint32_t scan_id;        // S3-chosen, echoed on results + done
    uint16_t duration_ms;    // total scan budget across all channels
    uint8_t  passive;        // 1 = passive (default for legal compliance), 0 = active
    uint8_t  channel_count;  // 1..LINK_WIFI_MAX_CHANNELS
    uint8_t  channels[LINK_WIFI_MAX_CHANNELS];  // 1..165, only first channel_count valid
} __attribute__((packed));

// Per-AP record. SSID is space-padded and `ssid_len` says how many bytes are
// significant; the receiver should NOT assume null termination.
// auth_mode mirrors wifi_auth_mode_t from esp_wifi_types.h (0=OPEN,
// 1=WEP, 2=WPA-PSK, 3=WPA2-PSK, 4=WPA/WPA2-PSK, 5=WPA2-ENTERPRISE,
// 6=WPA3-PSK, 7=WPA2/WPA3-PSK, 8=WAPI-PSK, 9=OWE, …).
// phy_modes bits: 0=11b, 1=11g, 2=11n, 3=11ac, 4=11ax, 5=LR, 6=WPS.
struct link_wifi_ap_result {
    uint32_t scan_id;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  bssid[LINK_WIFI_BSSID_LEN];
    uint8_t  auth_mode;
    uint8_t  ssid_len;
    char     ssid[LINK_WIFI_SSID_MAX];
    uint8_t  phy_modes;
    uint8_t  reserved[3];
} __attribute__((packed));

struct link_wifi_scan_done {
    uint32_t scan_id;
    uint16_t ap_count;       // how many AP_RESULT frames preceded this DONE
    uint16_t duration_ms;    // actual elapsed time
    uint8_t  status;         // link_wifi_scan_status
    uint8_t  reserved[3];
} __attribute__((packed));

// ── BLE scan (stage 5) ─────────────────────────────────────────────────────
//
// Same shape as Wi-Fi: S3 sends BLE_SCAN_REQ, C5 streams one BLE_ADV per
// observed advertising event, then closes with BLE_SCAN_DONE. The S3
// applies its existing bleDeviceCache MAC dedup so legacy-PHY adverts the
// S3 already saw via its own NimBLE-Arduino scan don't double-count; LE
// Coded / extended-adv records the S3 can't observe pass through naturally.

#define LINK_BLE_ADV_DATA_MAX  62   // legacy 31 + scan response 31; truncate beyond
#define LINK_BLE_ADDR_LEN      6

struct link_ble_scan_req {
    uint32_t scan_id;
    uint16_t duration_ms;       // total scan time before SCAN_DONE
    uint8_t  phy_mask;          // link_ble_phy_mask bits (1M / 2M / Coded)
    uint8_t  active;            // 1 = active (request scan response), 0 = passive
    uint16_t interval_ms;       // scan interval (0 = NimBLE default)
    uint16_t window_ms;         // scan window  (0 = NimBLE default)
    uint8_t  reserved[2];
} __attribute__((packed));

// Single advertising event. `adv_data_len` says how many `adv_data` bytes
// are significant (the rest are zero-padded). If the C5 sees more than
// LINK_BLE_ADV_DATA_MAX bytes (extended adv), it truncates and emits the
// frame anyway — adv_data_len reports what fit.
struct link_ble_adv {
    uint32_t scan_id;
    uint8_t  addr[LINK_BLE_ADDR_LEN];
    uint8_t  addr_type;         // 0=public, 1=random, 2=public-id, 3=random-id
    int8_t   rssi;
    uint8_t  primary_phy;       // 1=1M, 3=Coded (extended adv only otherwise 0)
    uint8_t  secondary_phy;     // 0=none, 1=1M, 2=2M, 3=Coded
    int8_t   tx_power;          // dBm if known, INT8_MIN (-128) if unknown
    uint8_t  adv_type;          // BLE_HCI_ADV_TYPE_* (0=ADV_IND, 1=ADV_DIRECT, etc.)
    uint8_t  adv_data_len;      // 0..LINK_BLE_ADV_DATA_MAX
    uint8_t  adv_data[LINK_BLE_ADV_DATA_MAX];
} __attribute__((packed));

struct link_ble_scan_done {
    uint32_t scan_id;
    uint16_t adv_count;
    uint16_t duration_ms;
    uint8_t  status;            // link_ble_scan_status
    uint8_t  reserved[3];
} __attribute__((packed));

// ── IEEE 802.15.4 scan (stage 6) ───────────────────────────────────────────
//
// 802.15.4 frame sniffer: the C5 hops channels 11-26, runs the radio in
// promiscuous RX, parses each received MAC frame, classifies the
// protocol family (Zigbee / Thread / Matter / Unknown) on heuristics,
// and emits one DETECTION per frame. S3 stores them for cloud forwarding.
//
// Unlike Wi-Fi / BLE, the S3 has no native 802.15.4 radio — this is
// a C5-only feature, not a "mirror". DETECTION frames carry both the
// parsed view and the raw MAC payload (truncated to 64 bytes) so
// diginode-cc can do deeper protocol analysis without re-asking the C5.

#define LINK_IEEE_CHANNEL_COUNT_MAX  16   // channels 11..26
#define LINK_IEEE_PAYLOAD_MAX        64
#define LINK_IEEE_ADDR_LEN           8    // EUI-64; short addrs occupy low 2 bytes

struct link_ieee_scan_req {
    uint32_t scan_id;
    uint16_t duration_ms;       // total scan budget; divided across channels
    uint8_t  channel_count;     // 1..LINK_IEEE_CHANNEL_COUNT_MAX
    uint8_t  channels[LINK_IEEE_CHANNEL_COUNT_MAX];  // 11..26 each
    uint8_t  reserved;
} __attribute__((packed));

// Parsed MAC-layer view of one captured 802.15.4 frame.
//
// Address fields use 8 bytes for both short and extended addresses. For
// short (16-bit) addresses, only the first two bytes are meaningful and
// the rest are zero. addr_mode tells you which is which:
//   0 = no address present
//   2 = short  (16-bit)
//   3 = extended (64-bit EUI)
//
// flags bits: 0=security_enabled, 1=ack_request, 2=frame_pending,
//             3=pan_id_compression, 4=seq_num_suppression, 5=ie_present.
struct link_ieee_detection {
    uint32_t scan_id;
    uint8_t  channel;           // 11..26
    int8_t   rssi;              // dBm reported by the radio
    uint8_t  lqi;               // link quality indicator (0..255)
    uint8_t  frame_type;        // link_ieee_frame_type
    uint8_t  frame_version;     // 0=2003, 1=2006, 2=2015
    uint8_t  protocol_family;   // link_ieee_protocol_family
    uint8_t  seq_num;           // sequence number (0 if suppressed)
    uint8_t  flags;
    uint16_t dst_pan;           // 0xFFFF when absent or broadcast
    uint16_t src_pan;           // 0xFFFF when absent (PAN ID compression)
    uint8_t  dst_addr_mode;
    uint8_t  dst_addr[LINK_IEEE_ADDR_LEN];
    uint8_t  src_addr_mode;
    uint8_t  src_addr[LINK_IEEE_ADDR_LEN];
    uint8_t  payload_len;       // 0..LINK_IEEE_PAYLOAD_MAX (truncated)
    uint8_t  payload[LINK_IEEE_PAYLOAD_MAX];
} __attribute__((packed));

struct link_ieee_scan_done {
    uint32_t scan_id;
    uint16_t detection_count;
    uint16_t duration_ms;
    uint8_t  status;            // link_ieee_scan_status
    uint8_t  reserved[3];
} __attribute__((packed));

// ── Expansion bus (stage 7) ────────────────────────────────────────────────
//
// Synchronous request/response semantics: the S3 sends a *_REQ frame
// carrying a request_id, the C5 executes the operation against its
// expansion I²C bus or one of the EXP_GPIOn pins, and replies with a
// matching *_RESP frame echoing the request_id. Both sides allow a
// single outstanding op for simplicity; the S3 side serialises via a
// mutex inside c5_link.cpp.

#define LINK_I2C_DATA_MAX     64    // bytes per read or write
#define LINK_EXP_GPIO_COUNT    5    // EXP_GPIO0..EXP_GPIO4 on J_EXP

// I²C read request. If reg_present is non-zero, the C5 writes `reg` to
// the device before issuing the repeated-start read.
struct link_i2c_read_req {
    uint32_t request_id;
    uint8_t  bus;               // reserved for future buses; 0 = expansion
    uint8_t  device_addr;       // 7-bit, no R/W bit baked in
    uint8_t  reg;               // register byte (ignored if reg_present=0)
    uint8_t  reg_present;
    uint8_t  read_len;          // 1..LINK_I2C_DATA_MAX
    uint8_t  reserved[3];
} __attribute__((packed));

struct link_i2c_read_resp {
    uint32_t request_id;
    uint8_t  status;            // link_exp_status
    uint8_t  read_len;          // actual bytes in `data` on success
    uint8_t  reserved[2];
    uint8_t  data[LINK_I2C_DATA_MAX];
} __attribute__((packed));

struct link_i2c_write_req {
    uint32_t request_id;
    uint8_t  bus;
    uint8_t  device_addr;
    uint8_t  reg;
    uint8_t  reg_present;
    uint8_t  data_len;          // 0..LINK_I2C_DATA_MAX
    uint8_t  reserved[2];
    uint8_t  data[LINK_I2C_DATA_MAX];
} __attribute__((packed));

struct link_i2c_write_resp {
    uint32_t request_id;
    uint8_t  status;
    uint8_t  reserved[3];
} __attribute__((packed));

// Unified GPIO request: same struct serves config / write / read,
// distinguished by `op`.
struct link_gpio_req {
    uint32_t request_id;
    uint8_t  pin_index;         // 0..LINK_EXP_GPIO_COUNT-1
    uint8_t  op;                // link_gpio_op
    uint8_t  mode;              // link_gpio_mode (CONFIG only)
    uint8_t  value;             // 0 or 1 (WRITE only)
    uint8_t  reserved[4];
} __attribute__((packed));

struct link_gpio_resp {
    uint32_t request_id;
    uint8_t  status;
    uint8_t  value;             // READ result; 0 otherwise
    uint8_t  reserved[2];
} __attribute__((packed));

// STATUS payload — minimal v1 health beacon.
struct link_status_payload {
    uint32_t uptime_ms;
    uint32_t free_heap;     // bytes of free heap on the sender
    uint16_t fw_version;    // 0x0100 = v1.0, 0x0101 = v1.0.1, …
    uint16_t rx_frame_ok;   // sender's decoder ok-count, wraps
    uint16_t rx_frame_err;  // sum of bad_crc + bad_length + short_frame + overflow
    uint16_t reserved;
} __attribute__((packed));
