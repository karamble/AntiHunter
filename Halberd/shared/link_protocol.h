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
    LINK_MSG_PING    = 0x01,  // either side → other side, with peer uptime
    LINK_MSG_PONG    = 0x02,  // reply to PING, echoes the original payload
    LINK_MSG_GPS_FIX = 0x10,  // C5 → S3, pushed at 1 Hz when NMEA flows
    LINK_MSG_STATUS  = 0xF0,  // periodic health beacon (sender-initiated)
    LINK_MSG_LOG     = 0xFE,  // forwarded log line (C5 → S3, future stage)
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

// STATUS payload — minimal v1 health beacon.
struct link_status_payload {
    uint32_t uptime_ms;
    uint32_t free_heap;     // bytes of free heap on the sender
    uint16_t fw_version;    // 0x0100 = v1.0, 0x0101 = v1.0.1, …
    uint16_t rx_frame_ok;   // sender's decoder ok-count, wraps
    uint16_t rx_frame_err;  // sum of bad_crc + bad_length + short_frame + overflow
    uint16_t reserved;
} __attribute__((packed));
