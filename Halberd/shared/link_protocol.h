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
    LINK_MSG_PING   = 0x01,  // either side → other side, with peer uptime
    LINK_MSG_PONG   = 0x02,  // reply to PING, echoes the original payload
    LINK_MSG_STATUS = 0xF0,  // periodic health beacon (sender-initiated)
    LINK_MSG_LOG    = 0xFE,  // forwarded log line (C5 → S3, future stage)
};

// PING / PONG payload.  The receiver echoes this verbatim in the PONG so the
// sender can compute round-trip time as (now - uptime_ms).
struct link_ping_payload {
    uint32_t uptime_ms;
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
