# Adding a link message type

When a new feature needs the C5 coprocessor's help, the standard
shape is a "trio": one S3 → C5 request (`*_REQ`), zero-or-more
C5 → S3 events streaming back, one C5 → S3 done marker (`*_DONE`).
This page is the recipe for adding a trio cleanly.

The most recent worked example is stage 8 (commit `00443a1`),
which added the WIFI_PROBE trio (0x23 / 0x24 / 0x25) end-to-end.
Skim that commit alongside this page.

## Code surfaces to touch

A complete trio touches every layer:

1. **`Halberd/shared/link_protocol.h`**. Three new values in
 `enum link_msg_type` and three packed structs.
2. **`Halberd/c5/main/link.{h,c}`**. Declare + implement
 `link_send_*_event`, `link_send_*_done`, plus
 `link_register_*_req` and a dispatcher case.
3. **A new C5 module** (e.g. `wifi_sniff.{h,c}`). Owns the request
 queue, the worker task, and (where applicable) coordinates radio
 access via the shared `wifi_radio_mutex`.
4. **`Halberd/c5/main/main.c`**. Call your new module's `init` in
 the right order (after dependencies).
5. **`Halberd/c5/main/CMakeLists.txt`**. Add your new `.c` to
 `SRCS`.
6. **`Halberd/{full,headless}/src/c5_link.{h,cpp}`**. Add the S3
 mirror struct, a queue, a start/drain/done trio matching the
 existing Wi-Fi / BLE / IEEE patterns.
7. **The consumer code**. `scanner.cpp` / `network.cpp` / wherever
 the new capability lands in the S3 surface.

Both `halberd-full` and `halberd-headless` get the same
`c5_link.{h,cpp}` patch. They're identical files by convention.
Stage 8 confirmed this by `diff -q`-ing them before and after.

## Conventions

### Endianness + packing

Multi-byte integers are little-endian on the wire, which matches
both ESP32 hosts natively. `__attribute__((packed))` everywhere.
Add `uint8_t reserved[N]` padding if you'd otherwise hit
ABI-fragile odd sizes. The existing structs round to 4-byte or
8-byte multiples wherever convenient.

### Message-type grouping

Type bytes are grouped by feature. Pick a free byte in the right
group:

| Range | Feature | Existing |
|---|---|---|
| `0x0x` | Control | PING, PONG, … |
| `0x1x` | GPS | GPS_FIX |
| `0x2x` | Wi-Fi | SCAN trio, PROBE trio |
| `0x3x` | BLE | SCAN trio |
| `0x4x` | 802.15.4 | SCAN trio |
| `0x5x` | I²C | READ + WRITE req/resp pairs |
| `0x6x` | GPIO | REQ + RESP |
| `0xFx` | Housekeeping | STATUS, LOG (future) |

Plenty of room within each group. No need to renumber existing
messages.

### Status codes

Reuse the existing `link_*_status` enum (OK / BUSY / ERROR) when
your trio is "start something on the C5 radio". Define a new enum
only if you genuinely need different status semantics.

### `scan_id` correlation

For trios with streaming events, the request carries a uint32
`scan_id` chosen by the S3. The C5 echoes it on every event and
on the closing DONE. The S3 uses it to correlate (especially
useful when a previous scan's stragglers arrive after a new scan
has started).

The convention is `scan_id = millis()` at the issue site.

### Queue depth on the S3

`c5_link.cpp` keeps one FreeRTOS queue per result type. Pick a
depth based on event rate:

| Result type | Depth | Rationale |
|---|---|---|
| Wi-Fi AP records | 64 | One scan = a few dozen APs max |
| BLE adverts | 128 | Several per second per device, multiple devices |
| 802.15.4 detections | 32 | Beacons every ~100 ms, lower rate than BLE |
| Probe events | 256 | Bursty. Many per second during active environments |

Drop-oldest-on-overflow is the existing pattern:

```cpp
if (xQueueSend(s_your_queue, &ev, 0) != pdTRUE) {
    YourStruct drop;
    xQueueReceive(s_your_queue, &drop, 0);
    xQueueSend(s_your_queue, &ev, 0);
}
```

### Radio mutex

If your trio uses the C5's 2.4 GHz front-end (BLE, Wi-Fi 2.4,
802.15.4), serialise with `wifi_radio_mutex` (declared in
`wifi.h`). For 5 GHz Wi-Fi, the same mutex applies. `wifi.c` and
`wifi_sniff.c` both take it. For separate radios (e.g. Expansion
I²C is bus-only), no mutex needed.

A REQ that arrives while the mutex is held should immediately emit
a DONE with `status = BUSY` and return. Don't queue and don't
block. The S3 retries.

## Backwards compatibility

The wire protocol has **no version negotiation**. A C5 firmware
ahead of the S3 firmware (or vice-versa) silently drops unknown
message types with a one-line warn log. This works because:

- The S3 only sends messages the firmware pair supports (implicit
 via the build pair).
- The C5 only sends events the S3 subscribed to via a REQ.
- A missing trio degrades to "feature unavailable" rather than
 corrupting state.

If you ever need real version negotiation, add it as a new control
message type in the `0x0x` group rather than retrofitting.

## Worked example. Stage 8 WIFI_PROBE trio

The stage 8 commit (`00443a1`) is the canonical recent example. It
added:

1. **Shared protocol** (`link_protocol.h`):
 - `LINK_MSG_WIFI_PROBE_REQ` = 0x23
 - `LINK_MSG_WIFI_PROBE_EVENT` = 0x24
 - `LINK_MSG_WIFI_PROBE_DONE` = 0x25
 - `struct link_wifi_probe_req` (40 B), `_event` (158 B),
 `_done` (12 B).

2. **C5 wifi.h**. Exposed `wifi_radio_mutex` so the new sniff
 module could share it with the existing scan task.

3. **C5 wifi.c**. Replaced the legacy `s_scan_busy` bool with
 `xSemaphoreTake(wifi_radio_mutex, 0)`, emits BUSY status on
 contention.

4. **New `Halberd/c5/main/wifi_sniff.{h,c}`**. Promiscuous-mode
 RX, ISR callback filters mgmt frames (FCF type=0, stype=4 or 5),
 channel-hops the supplied list with 80–500 ms dwell per channel,
 emits one EVENT per captured frame, closes with DONE.

5. **C5 link.{h,c}**. Three new `link_send_*` helpers + one new
 `link_register_*` + one dispatcher case.

6. **C5 main.c**. `wifi_sniff_init()` called after `wifi_init()`
 (which creates the mutex).

7. **C5 CMakeLists.txt**. `wifi_sniff.c` added to `SRCS`.

8. **S3 `c5_link.{h,cpp}`** (both variants). `C5WifiProbeEvent`
 struct, 256-slot queue, three handlers in `on_frame()` (EVENT
 decode + drop-oldest queue insert, DONE marker capture, a
 stub for ACK in the future).

9. **S3 `scanner.cpp`** (both variants). Refactored
 `probeDetectionTask` inner loop into
 `processProbeRequestEvent()` so the new C5-side drain
 (`drainC5WifiProbes()`) could feed the same merge-into-
 `probeDevices` + `sendProbeHitMesh()` pipeline as the existing
 2.4 GHz ISR queue. New `triggerC5WifiProbeSniff()` re-arms the
 C5 every 10 s with 9 s sniff windows.

**Total**: 15 files modified + 2 new, ~1045 lines added, build
sizes (C5 +50 KB, S3 unchanged), and zero behaviour change for
existing 2.4 GHz capture.

## Pitfalls

- **Struct size matches across builds**. PIO (Arduino) and IDF
 build the same `link_protocol.h`. The `__attribute__((packed))`
 attribute is portable but verify struct sizes match if you add a
 field. The `wifi_sniff` event struct was sized to 158 B
 deliberately to round neatly.
- **Cmake source list**. Don't forget to add the new `.c` to
 `Halberd/c5/main/CMakeLists.txt`. `idf.py` won't auto-detect.
- **Init order**. If your new module depends on `wifi_init`'s
 setup (the radio mutex, the country code, the Wi-Fi stack
 itself), call its init *after* `wifi_init()` in `main.c`.
- **S3-side parity**. `halberd-full` and `halberd-headless` share
 `c5_link.{h,cpp}` content but not the file. Patch both.

## See also

- [Link protocol](./protocols/link.md). The wire-format spec
 the new trio plugs into.
- [C5 coprocessor](./firmware/c5-coprocessor.md). The responder
 context.
- [Decisions log](./decisions.md). Framing + scheduling rationale.
