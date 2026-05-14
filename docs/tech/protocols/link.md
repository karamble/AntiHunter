# S3 ↔ C5 link protocol

The Halberd S3 and C5 talk to each other over a 921600-baud UART
using a custom framing layer: **COBS** byte stuffing + **CRC-16/
CCITT-FALSE** + a single-byte message type + a single-byte length +
a packed binary payload. This page is the authoritative wire-format
reference. The header file at
[`Halberd/shared/link_protocol.h`](https://github.com/karamble/halberd/blob/main/Halberd/shared/link_protocol.h)
is the single source of truth. If it disagrees with this page, the
header wins.

## Physical wiring

| C5 pin | S3 pin | Net (from S3's perspective) | Direction |
|---|---|---|---|
| D6 / GPIO11 (UART0 TX on C5 silkscreen) | D7 / GPIO44 (RX) | `C5_LINK_RX` | C5 transmits |
| D7 / GPIO12 (UART0 RX on C5 silkscreen) | D6 / GPIO43 (TX) | `C5_LINK_TX` | S3 transmits |

Net names are from the S3's perspective, so on the C5 side
`C5_LINK_TX` is on the chip's RX pin. UART is 8N1, no flow control,
921600 baud.

## Frame format

```
On the wire:
   [ COBS-encoded body ] [ 0x00 ]

Pre-encoding body:
   [type:1] [seq:1] [len:1] [payload:0..255] [crc16_lo:1] [crc16_hi:1]
```

- **type**. 1 byte, see [message-type registry](#message-type-registry).
- **seq**. 1 byte, sender-incrementing counter (wraps freely.
 receivers echo it in replies so the sender can correlate).
- **len**. 1 byte, payload length 0.255.
- **payload**. `len` bytes. Layout depends on type.
- **crc16**. CRC-16/CCITT-FALSE over `[type][seq][len][payload]`,
 transmitted little-endian.

COBS encoding turns 0x00 bytes in the body into offset markers and
appends a single 0x00 frame delimiter. The decoder scans for 0x00
to find frame boundaries, COBS-decodes the preceding bytes, then
verifies the CRC.

Worst-case wire bytes for a 255-byte payload ≈ 263.

### CRC variant

`CRC-16/CCITT-FALSE`:

| Parameter | Value |
|---|---|
| Polynomial | `0x1021` |
| Init | `0xFFFF` |
| RefIn / RefOut | false |
| XorOut | 0 |

Same as XMODEM-CRC. Implementation in
[`Halberd/shared/link_codec.c`](https://github.com/karamble/halberd/blob/main/Halberd/shared/link_codec.c)
function `link_crc16`. Standard 8-bit-at-a-time loop, no lookup
table.

### Why COBS, not SLIP

COBS gives constant 1-byte-per-254 overhead, a single sync byte
(`0x00`), and no escape sequences. SLIP would work but the escape
character makes the receiver state machine larger. JSON would be
fine at 921600 baud for today's message rates but won't keep up
with high-rate BLE adv / Wi-Fi mass-scan streams.

## Message-type registry

`enum link_msg_type` in
[`Halberd/shared/link_protocol.h`](https://github.com/karamble/halberd/blob/main/Halberd/shared/link_protocol.h):

| Hex | Name | Direction | Stage | Payload struct |
|---:|---|---|---:|---|
| 0x01 | `LINK_MSG_PING` | bidirectional | 2 | `link_ping_payload` (4 B) |
| 0x02 | `LINK_MSG_PONG` | bidirectional | 2 | `link_ping_payload` (echoed) |
| 0x10 | `LINK_MSG_GPS_FIX` | C5 → S3 (1 Hz) | 3 | `link_gps_fix` (32 B) |
| 0x20 | `LINK_MSG_WIFI_SCAN_REQ` | S3 → C5 | 4 | `link_wifi_scan_req` (40 B) |
| 0x21 | `LINK_MSG_WIFI_AP_RESULT` | C5 → S3 | 4 | `link_wifi_ap_result` (49 B) |
| 0x22 | `LINK_MSG_WIFI_SCAN_DONE` | C5 → S3 | 4 | `link_wifi_scan_done` (12 B) |
| 0x23 | `LINK_MSG_WIFI_PROBE_REQ` | S3 → C5 | 8 | `link_wifi_probe_req` (40 B) |
| 0x24 | `LINK_MSG_WIFI_PROBE_EVENT` | C5 → S3 | 8 | `link_wifi_probe_event` (158 B) |
| 0x25 | `LINK_MSG_WIFI_PROBE_DONE` | C5 → S3 | 8 | `link_wifi_probe_done` (12 B) |
| 0x30 | `LINK_MSG_BLE_SCAN_REQ` | S3 → C5 | 5 | `link_ble_scan_req` (14 B) |
| 0x31 | `LINK_MSG_BLE_ADV` | C5 → S3 | 5 | `link_ble_adv` (82 B) |
| 0x32 | `LINK_MSG_BLE_SCAN_DONE` | C5 → S3 | 5 | `link_ble_scan_done` (12 B) |
| 0x40 | `LINK_MSG_IEEE_SCAN_REQ` | S3 → C5 | 6 | `link_ieee_scan_req` (22 B) |
| 0x41 | `LINK_MSG_IEEE_DETECTION` | C5 → S3 | 6 | `link_ieee_detection` (~110 B) |
| 0x42 | `LINK_MSG_IEEE_SCAN_DONE` | C5 → S3 | 6 | `link_ieee_scan_done` (12 B) |
| 0x50 | `LINK_MSG_I2C_READ_REQ` | S3 → C5 | 7 | `link_i2c_read_req` (12 B) |
| 0x51 | `LINK_MSG_I2C_READ_RESP` | C5 → S3 | 7 | `link_i2c_read_resp` (72 B) |
| 0x52 | `LINK_MSG_I2C_WRITE_REQ` | S3 → C5 | 7 | `link_i2c_write_req` (76 B) |
| 0x53 | `LINK_MSG_I2C_WRITE_RESP` | C5 → S3 | 7 | `link_i2c_write_resp` (8 B) |
| 0x60 | `LINK_MSG_GPIO_REQ` | S3 → C5 | 7 | `link_gpio_req` (12 B) |
| 0x61 | `LINK_MSG_GPIO_RESP` | C5 → S3 | 7 | `link_gpio_resp` (8 B) |
| 0xF0 | `LINK_MSG_STATUS` | bidirectional (30 s) | 2 | `link_status_payload` (16 B) |
| 0xFE | `LINK_MSG_LOG` | C5 → S3 (future) |. | TBD |

Type bytes are grouped by feature: `0x0x` control, `0x1x` GPS,
`0x2x` Wi-Fi, `0x3x` BLE, `0x4x` 802.15.4, `0x5x` I²C, `0x6x` GPIO,
`0xFx` housekeeping. Plenty of room to grow within each group
without renumbering.

## Payload structs

All structs are `__attribute__((packed))`. Multi-byte integers are
little-endian.

### `link_ping_payload`. 4 bytes

```c
struct link_ping_payload {
    uint32_t uptime_ms;   // sender's millis() when this PING was built
};
```

The receiver replies with `PONG` carrying the same bytes verbatim so
the originator computes round-trip time as `now - uptime_ms`.

### `link_gps_fix`. 32 bytes

```c
struct link_gps_fix {
    uint32_t uptime_ms;        // C5 uptime at fix assembly
    uint8_t  fix_valid;        // 0 = no fix, 1 = 2D/3D fix usable
    uint8_t  satellites;       // from $xxGGA
    uint16_t hdop_x100;        // HDOP × 100 (123 = 1.23)
    int32_t  lat_e7;           // degrees × 1e7, north positive
    int32_t  lon_e7;           // degrees × 1e7, east positive
    int16_t  altitude_m;       // m above mean sea level
    uint16_t speed_kmh_x10;    // km/h × 10
    uint16_t course_deg_x10;   // degrees × 10
    uint16_t year;             // e.g. 2026 (UTC)
    uint8_t  month, day, hour, minute, second, centisecond;
    uint8_t  date_valid;       // 1 if RMC reported a usable date
    uint8_t  time_valid;       // 1 if RMC reported a usable time
    uint8_t  reserved;
};
```

The C5 pushes one per second from `gps.c::gps_task`. The S3 caches
the most recent frame and treats it as authoritative until 30 s
elapse without a new frame. See `hardware.cpp::updateGPSLocation`,
now a freshness watchdog rather than a UART reader.

### Wi-Fi scan triplet (stage 4)

```c
// S3 → C5
struct link_wifi_scan_req {
    uint32_t scan_id;        // S3-chosen, echoed on every result + DONE
    uint16_t duration_ms;    // total scan budget across all channels
    uint8_t  passive;        // 1 = passive (default), 0 = active
    uint8_t  channel_count;  // 1..32
    uint8_t  channels[32];   // 1..165, only first channel_count valid
};

// C5 → S3, one per AP seen
struct link_wifi_ap_result {
    uint32_t scan_id;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  bssid[6];
    uint8_t  auth_mode;      // wifi_auth_mode_t (0=OPEN, 1=WEP, …)
    uint8_t  ssid_len;       // 0..32 (SSID NOT NUL-terminated when 32)
    char     ssid[32];
    uint8_t  phy_modes;      // bit0=11b … bit4=11ax, bit5=LR, bit6=WPS
    uint8_t  reserved[3];
};

// C5 → S3, closes the scan
struct link_wifi_scan_done {
    uint32_t scan_id;
    uint16_t ap_count;
    uint16_t duration_ms;    // actual elapsed time
    uint8_t  status;         // 0=OK, 1=BUSY, 2=ERROR
    uint8_t  reserved[3];
};
```

`duration_ms` is the total budget. The C5 divides by `channel_count`
and clamps each per-channel dwell to `[80, 500] ms`. AP_RESULT
frames stream first, then the matching DONE. A REQ that arrives
mid-scan gets an immediate DONE with `status = BUSY`.

### Wi-Fi probe sniff triplet (stage 8)

```c
// S3 → C5
struct link_wifi_probe_req {
    uint32_t scan_id;
    uint16_t duration_ms;        // 1..65000; total dwell across channels
    uint8_t  capture_responses;  // 1 = also capture stype 5 (probe resp)
    uint8_t  channel_count;      // 1..32
    uint8_t  channels[32];
};

// C5 → S3, one per captured 802.11 mgmt frame
struct link_wifi_probe_event {
    uint32_t scan_id;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  is_response;        // 0 = probe req, 1 = probe resp
    uint8_t  reserved;
    uint8_t  src_mac[6];         // addr2 (probing client OR responding AP)
    uint8_t  dst_mac[6];         // addr1 (broadcast for req, client for resp)
    uint8_t  bssid[6];           // addr3
    uint16_t payload_len;        // 0..128
    uint8_t  payload[128];       // raw 802.11 mgmt frame from FCF onward
};

// C5 → S3
struct link_wifi_probe_done {
    uint32_t scan_id;
    uint16_t event_count;
    uint16_t duration_ms;
    uint8_t  status;             // reuses link_wifi_scan_status
    uint8_t  reserved[3];
};
```

The probe sniffer puts the C5 Wi-Fi stack into promiscuous mode and
hops the supplied channel list with 80–500 ms dwell per channel.
ISR filters mgmt frames (FCF type=0), then stype=4 (probe req)
always and stype=5 (probe resp) only when `capture_responses=1`.
Coordinates radio access with the active-scan task via a shared
`wifi_radio_mutex`.

### BLE scan triplet (stage 5)

```c
// S3 → C5
struct link_ble_scan_req {
    uint32_t scan_id;
    uint16_t duration_ms;
    uint8_t  phy_mask;           // 0x01=1M, 0x02=2M, 0x04=Coded
    uint8_t  active;             // 1 = active (request scan response)
    uint16_t interval_ms;        // 0 = NimBLE default
    uint16_t window_ms;          // 0 = NimBLE default (≤ interval_ms)
    uint8_t  reserved[2];
};

// C5 → S3, one per advertisement event
struct link_ble_adv {
    uint32_t scan_id;
    uint8_t  addr[6];
    uint8_t  addr_type;          // 0=public, 1=random, 2=public-id, 3=random-id
    int8_t   rssi;
    uint8_t  primary_phy;        // 1=1M, 3=Coded, 0=unknown
    uint8_t  secondary_phy;      // 0=none, 1=1M, 2=2M, 3=Coded
    int8_t   tx_power;           // dBm, INT8_MIN if unknown
    uint8_t  adv_type;
    uint8_t  adv_data_len;       // 0..62
    uint8_t  adv_data[62];       // truncated beyond 62
};

// C5 → S3
struct link_ble_scan_done {
    uint32_t scan_id;
    uint16_t adv_count;
    uint16_t duration_ms;
    uint8_t  status;             // 0=OK, 1=BUSY, 2=ERROR
    uint8_t  reserved[3];
};
```

C5 defaults to passive scans with `1M + Coded`. Same single-scan-
at-a-time semantics as Wi-Fi. Concurrent REQ → DONE+BUSY.

### IEEE 802.15.4 detection triplet (stage 6)

```c
// S3 → C5
struct link_ieee_scan_req {
    uint32_t scan_id;
    uint16_t duration_ms;        // total budget; divided across channels
    uint8_t  channel_count;      // 1..16
    uint8_t  channels[16];       // each 11..26
    uint8_t  reserved;
};

// C5 → S3, one per parsed frame
struct link_ieee_detection {
    uint32_t scan_id;
    uint8_t  channel;            // 11..26
    int8_t   rssi;
    uint8_t  lqi;
    uint8_t  frame_type;         // 0=beacon, 1=data, 2=ack, 3=cmd, …
    uint8_t  frame_version;      // 0=2003, 1=2006, 2=2015
    uint8_t  protocol_family;    // 0=Unknown, 1=Zigbee, 2=Thread, 3=Matter, 99=Other
    uint8_t  seq_num;
    uint8_t  flags;              // bit0=security … bit5=ie_present
    uint16_t dst_pan;            // 0xFFFF if absent / broadcast
    uint16_t src_pan;            // 0xFFFF if absent (PAN ID compression)
    uint8_t  dst_addr_mode;      // 0=none, 2=short, 3=extended
    uint8_t  dst_addr[8];        // EUI-64 if mode=3, low 2 bytes if mode=2
    uint8_t  src_addr_mode;
    uint8_t  src_addr[8];
    uint8_t  payload_len;        // 0..64
    uint8_t  payload[64];        // MAC payload / beacon payload
};

// C5 → S3
struct link_ieee_scan_done {
    uint32_t scan_id;
    uint16_t detection_count;
    uint16_t duration_ms;
    uint8_t  status;
    uint8_t  reserved[3];
};
```

C5 runs the 802.15.4 radio in promiscuous mode, parses each frame
at the MAC layer, and classifies the protocol family with
heuristics (Zigbee Stack Profile byte for beacons, 6LoWPAN
dispatch byte / PAN ID for data + command frames).

### Expansion bus request/response (stage 7)

Synchronous request/response over the link. The S3 generates a
`request_id`, sends a `*_REQ`, blocks until the matching `*_RESP`
arrives or the timeout expires. Only one expansion op may be in
flight. The S3-side helper serialises with a mutex.

```c
// I²C read
struct link_i2c_read_req {
    uint32_t request_id;
    uint8_t  bus;                // reserved; 0 = expansion
    uint8_t  device_addr;        // 7-bit, no R/W bit
    uint8_t  reg;                // register byte
    uint8_t  reg_present;        // 0 = raw read, 1 = write `reg` first
    uint8_t  read_len;           // 1..64
    uint8_t  reserved[3];
};

struct link_i2c_read_resp {
    uint32_t request_id;
    uint8_t  status;             // 0=OK, 1=NACK, 2=TIMEOUT, 3=BUSY, 4=BAD_PARAM, 5=NOT_READY
    uint8_t  read_len;
    uint8_t  reserved[2];
    uint8_t  data[64];
};

// I²C write
struct link_i2c_write_req {
    uint32_t request_id;
    uint8_t  bus;
    uint8_t  device_addr;
    uint8_t  reg;
    uint8_t  reg_present;
    uint8_t  data_len;           // 0..64
    uint8_t  reserved[2];
    uint8_t  data[64];
};

struct link_i2c_write_resp {
    uint32_t request_id;
    uint8_t  status;
    uint8_t  reserved[3];
};

// GPIO — one struct, three ops
struct link_gpio_req {
    uint32_t request_id;
    uint8_t  pin_index;          // 0..4 = EXP_GPIO0..EXP_GPIO4
    uint8_t  op;                 // 0=CONFIG, 1=WRITE, 2=READ
    uint8_t  mode;               // CONFIG only: 0=INPUT, 1=OUTPUT, 2=INPUT_PULLUP, 3=INPUT_PULLDOWN, 4=OPEN_DRAIN
    uint8_t  value;              // WRITE only: 0 or 1
    uint8_t  reserved[4];
};

struct link_gpio_resp {
    uint32_t request_id;
    uint8_t  status;
    uint8_t  value;              // READ result; 0 otherwise
    uint8_t  reserved[2];
};
```

### `link_status_payload`. 16 bytes

```c
struct link_status_payload {
    uint32_t uptime_ms;
    uint32_t free_heap;          // bytes
    uint16_t fw_version;         // 0x0100 = v1.0
    uint16_t rx_frame_ok;        // sender's decoder ok-count (wraps)
    uint16_t rx_frame_err;       // bad_crc + bad_length + short_frame + overflow
    uint16_t reserved;
};
```

Both sides emit one every 30 s. `rx_frame_err` spiking means link
integrity is degrading. Usually a sign of a flaky UART connection
or a sender overrunning the receive buffer.

## Codec API

### Encoder

```c
size_t link_encode(uint8_t type, uint8_t seq,
                   const uint8_t *payload, size_t payload_len,
                   uint8_t *out, size_t out_cap);
```

Returns total bytes written (COBS-encoded body + trailing `0x00`).
Returns 0 on oversized payload or undersized output buffer.

### Streaming decoder

```c
typedef struct {
    uint8_t  buf[LINK_MAX_ENCODED];
    size_t   fill;
    bool     overflow;
    link_decoder_stats_t stats;   // ok, bad_crc, bad_length, short_frame, overflow
} link_decoder_t;

typedef void (*link_frame_cb)(void *ctx, uint8_t type, uint8_t seq,
                              const uint8_t *payload, size_t payload_len);

void link_decoder_init(link_decoder_t *d);
void link_decoder_feed(link_decoder_t *d, const uint8_t *bytes, size_t n,
                       link_frame_cb cb, void *ctx);
```

Feed bytes from UART RX. The callback fires once per validated
frame. Malformed frames are silently dropped, with the cause counted
in `stats`.

## Boot codec selftest

`Halberd/c5/main/link.c` runs a hard-coded round-trip on boot:

1. Build a frame with payload `{0x00, 0xFF, 0x42, 0x00, 0x01, 0xAA}`
 (contains zeros, so COBS gets exercised).
2. `link_encode` → `link_decoder_feed` → assert exact byte-for-byte
 match.
3. Log `selftest OK` or a diagnostic line with the decoder stats.

This catches codec arithmetic regressions without needing an S3
peer wired up. The S3 doesn't yet have an equivalent. See
[decisions](./decisions.md).

## Heartbeats and scheduling

- **PING / PONG**: each side every 5 s, offset slightly so they
 don't always interleave.
- **STATUS**: each side every 30 s.
- **GPS_FIX**: C5 → S3 every 1 s.
- **Scan jobs**: one at a time. The C5 holds a `wifi_radio_mutex`
 to serialise the active-scan task and the probe-sniff task on the
 Wi-Fi side, and the BLE / 802.15.4 / Wi-Fi front-ends share the
 2.4 GHz radio time-sliced by IDF.

## Debugging on the host

`make c5-monitor` shows C5-side log lines (`TAG=link`, `TAG=gps`,
`TAG=halberd-c5`) over the USB Serial/JTAG console, **not** the
on-wire UART bytes. To watch actual link traffic you need a logic
analyser or a USB-UART adapter tapped to the inter-chip wires.

A future build flag to dump every encoded frame as hex to the
USB Serial/JTAG console is a candidate convenience.

## See also

- [Adding a link message type](./extending/new-link-message.md).
 how to register a new trio.
- [C5 coprocessor](./firmware/c5-coprocessor.md). The responder
 side.
- [Decisions log](./decisions.md). Framing rationale.
