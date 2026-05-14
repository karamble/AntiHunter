# Adding a link message type

When a new feature needs the C5 coprocessor's help, the standard
shape is a "trio": one S3 → C5 request (`*_REQ`), zero-or-more
C5 → S3 events streaming back, one C5 → S3 done marker (`*_DONE`).
This page is the recipe for adding a trio cleanly.

> 🚧 **Stub page.** Content lands in a follow-up commit. The
> stage 8 commit `00443a1` (adding `LINK_MSG_WIFI_PROBE_REQ` /
> `LINK_MSG_WIFI_PROBE_EVENT` / `LINK_MSG_WIFI_PROBE_DONE`) is the
> most recent worked example.

## What this page will cover

### Code surfaces to touch

1. **`Halberd/shared/link_protocol.h`** — add three values to
   `enum link_msg_type` (group by feature byte: `0x2x` Wi-Fi, `0x3x`
   BLE, `0x4x` 802.15.4, …) and three packed structs (`link_*_req`,
   `link_*_event`, `link_*_done`).
2. **`Halberd/c5/main/link.{h,c}`** — declare + implement
   `link_send_*_event` and `link_send_*_done`, plus
   `link_register_*_req` and a dispatcher case.
3. **A new C5 module** (e.g. `wifi_sniff.{h,c}`) — owns the request
   queue, the worker task, and (where applicable) coordinates radio
   access via the shared `wifi_radio_mutex`.
4. **`Halberd/c5/main/main.c`** — call your new module's `init` in
   the right order.
5. **`Halberd/c5/main/CMakeLists.txt`** — add your new `.c` to
   `SRCS`.
6. **`Halberd/{full,headless}/src/c5_link.{h,cpp}`** — add the S3
   mirror struct, a queue, a start/drain/done trio matching the
   existing Wi-Fi / BLE / IEEE patterns.
7. **The consumer code** — `scanner.cpp` / `network.cpp` / wherever
   the new capability lands in the S3 surface.

### Conventions

- **Endianness**: little-endian on the wire, matches both hosts
  natively. `__attribute__((packed))` everywhere.
- **Sizes**: every payload struct should round to a reasonable
  alignment and have a stable size. Add `uint8_t reserved[N]` to pad
  if you'd otherwise hit ABI-fragile odd sizes.
- **Status field**: reuse the existing `link_*_status` enum (OK /
  BUSY / ERROR) when applicable.
- **scan_id**: caller-supplied uint32 echoed on every event + the
  done marker, so the S3 can correlate.
- **Queue depth on the S3**: pick based on event rate (Wi-Fi APs:
  64, BLE advs: 128, 802.15.4: 32, probe events: 256). Use the
  drop-oldest-on-overflow pattern from `c5_link.cpp`.
- **Single-radio mutex**: if you use the C5's 2.4 GHz front-end,
  serialise with the existing `wifi_radio_mutex` (or add a sibling
  mutex if you're on a separate radio).

### Backwards compatibility

The wire protocol has no version negotiation. A C5 firmware ahead
of the S3 firmware (or vice-versa) drops unknown message types
with a `WIFI_PROBE_REQ ... dropped` style log. This is fine because:
- the S3 only sends messages the C5 advertises supporting (implicit
  via the firmware pairing);
- the C5 only sends events the S3 has subscribed to;
- a missing trio just degrades to "feature unavailable" rather than
  corrupting state.

If you ever need real version negotiation, add it as a new control
message type in the `0x0x` group rather than retrofitting.

### Worked example

Stage 8 (`00443a1`) added the WIFI_PROBE trio (0x23 / 0x24 / 0x25)
end-to-end across all the surfaces above. Read that commit before
adding your own trio.

## See also

- [S3 ↔ C5 link protocol](../protocols/link.md) for the framing +
  registry the new trio plugs into.
- [C5 coprocessor](../firmware/c5-coprocessor.md) for context on
  the C5's responsibilities.
