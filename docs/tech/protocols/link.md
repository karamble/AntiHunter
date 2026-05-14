# S3 ↔ C5 link protocol

The Halberd S3 and C5 talk to each other over a 921600-baud UART
using a custom protocol: COBS framing + CRC-16/CCITT-FALSE + a
single-byte message type + a single-byte length + a packed binary
payload. This page is the authoritative wire-format reference.

> 🚧 **Stub page.** Content lands in a follow-up commit, sourced
> from the per-session wiki `05-link-protocol.md` and the source-of-
> truth header `Halberd/shared/link_protocol.h`.

## What this page will cover

### Framing

- COBS encoding rules (constant 1-byte-per-254 overhead, single
  sync byte 0x00, no escape sequences).
- CRC-16/CCITT-FALSE: polynomial, init value, no lookup table.
- Frame structure: [COBS-encoded ((type, seq, length, payload, crc))] [0x00].

### Message-type registry

Type bytes are grouped by feature: `0x0x` control, `0x1x` GPS,
`0x2x` Wi-Fi, `0x3x` BLE, `0x4x` 802.15.4, `0x5x` I²C, `0x6x` GPIO,
`0xFx` housekeeping.

Full table with direction (S3 → C5 / C5 → S3 / bidirectional),
payload struct name, payload size, and stage in which it was added.

### Payload structs

For each message type: the `struct link_*` definition (with packed
attribute), every field with units / range / meaning, and the
canonical S3-side mirror struct (where applicable).

Highlights:
- `link_ping_payload` / `link_pong` — round-trip-time heartbeat.
- `link_gps_fix` — 1 Hz push, GPS lat/lon/sats/hdop/datetime.
- `link_wifi_scan_req` / `link_wifi_ap_result` /
  `link_wifi_scan_done` — Wi-Fi scan trio (stage 4).
- `link_wifi_probe_req` / `link_wifi_probe_event` /
  `link_wifi_probe_done` — 5 GHz probe-sniffer trio (stage 8).
- `link_ble_scan_req` / `link_ble_adv` / `link_ble_scan_done` —
  BLE scan trio (stage 5).
- `link_ieee_scan_req` / `link_ieee_detection` /
  `link_ieee_scan_done` — 802.15.4 sniff trio (stage 6).
- `link_i2c_read_req` / `link_i2c_read_resp` /
  `link_i2c_write_req` / `link_i2c_write_resp` — synchronous I²C
  (stage 7).
- `link_gpio_req` / `link_gpio_resp` — synchronous EXP_GPIO
  (stage 7).
- `link_status_payload` — 30 s housekeeping beacon.

### Semantics

- ACK / BUSY / ERROR via the `*_DONE` `status` field.
- Single-radio scheduling on the C5 — BLE / Wi-Fi 2.4 / 802.15.4
  share the 2.4 GHz front-end. A REQ while another scan is running
  returns `BUSY`.
- Backpressure handling: queue depths, drop-oldest-on-overflow on
  the S3 side.
- The `wifi_radio_mutex` on the C5 that gates `wifi.c` and
  `wifi_sniff.c` against each other.

## See also

- [C5 coprocessor](../firmware/c5-coprocessor.md) for the responder
  side.
- [Extending: new link message type](../extending/new-link-message.md)
  for adding a fourth trio.
