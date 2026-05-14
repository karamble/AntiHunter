# Mesh protocol — halberd ↔ diginode-cc

Halberd nodes talk to each other and to the diginode-cc fleet server
over Meshtastic. The wire format is text frames on the
`TEXTMSG` channel — readable, line-oriented, dispatched on the first
token (`STATUS`, `SCAN_START`, `PROBE_START`, …). This page is the
integration reference for anyone implementing the protocol on a
non-Halberd peer.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## What this page will cover

### Transport

- Meshtastic `sendToSerial1` text frames, `MAX_MESH_SIZE` cap.
- Channel selection, region settings (`HELTEC_REGION` default
  `EU_868`).
- Heltec V3 configuration applied by
  `scripts/flash-heltec-meshtastic.sh`.
- Bandwidth realities — what fits in a frame, how a multi-frame
  reply is composed (no chunking today; replies stay under one
  frame).

### Message format

- `<NODE_ID>: <COMMAND>:<args>` line shape.
- ACK envelope: `<NODE_ID>: <COMMAND>_ACK:<STATUS>` where STATUS ∈
  `STARTED` / `BUSY` / `STOPPED` / `INVALID`.
- Result envelope: `<NODE_ID>: <KIND>:<payload>` for hits,
  heartbeats, anomalies, alerts.

### Command registry

Every command Halberd dispatches off the mesh — `STATUS`, `STOP`,
`SCAN_START`, `DEVICE_SCAN_START`, `PROBE_START`,
`BASELINE_START`, `DRONE_START`, `DEAUTH_START`,
`RANDOMIZATION_START`, `TRIANGULATE_START` / `TRIANGULATE_STOP` /
`TRIANGULATE_RESULTS`, `BATTERY_SAVER_START` / `STOP` / `STATUS`,
`AUTOERASE_ENABLE` / `DISABLE` / `STATUS`, `VIBRATION_ON` / `OFF` /
`STATUS`, `HB_ON` / `OFF` / `INTERVAL`, `RAW_BLE_*`,
`ERASE_REQUEST` / `ERASE_FORCE` / `ERASE_CANCEL`,
`CONFIG_TARGETS` / `CONFIG_NODEID` / `CONFIG_RSSI` /
`CONFIG_CHANNELS`, `SETTIME`.

For each: direction, argument grammar, expected reply, side effects,
typical timing.

### Result kinds

- `HIT` — target detection.
- `PROBE_HIT` — probe-mode hit.
- `DEAUTH_HIT` — deauth detection.
- `DRONE_HIT` — OpenDroneID detection.
- `ANOMALY` — baseline-deviation alert.
- `HEARTBEAT` — periodic status broadcast.
- `TRIANGULATE_RESULT` — multi-node fix.

### Rate limiting

Per-target dedup windows (60 s default for PROBE_HIT, 3 s default
for HIT), how to override per-deployment.

### Server-side parser

How `diginode-cc` consumes these frames (pointer into the
`diginode-cc` repo — out of scope for this doc, but covered by
that project's docs).

## See also

- User handbook: [Mesh command reference](../../user/commands.md) —
  the same registry from an operator perspective.
- [REST API reference](../api-rest.md) — the parallel HTTP
  interface on `halberd-full`.
