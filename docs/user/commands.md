# Mesh command reference

Every command a Halberd node accepts over Meshtastic, organised by
use case rather than alphabetically. Use this page as a lookup table
when you know what you want to do but don't remember the exact
command shape.

> 🚧 **Stub page.** Content lands in a follow-up commit. The
> [README's Mesh Commands section](../../README.md#mesh-commands)
> has the full list in the meantime.

## What this page will cover

- **Status + control**: `STATUS`, `STOP`, `SETTIME`.
- **Scanning**: `SCAN_START`, `DEVICE_SCAN_START`, `PROBE_START`,
  `BASELINE_START`, `RANDOMIZATION_START`.
- **Detection**: `DRONE_START`, `DEAUTH_START`.
- **Triangulation**: `TRIANGULATE_START`, `TRIANGULATE_STOP`,
  `TRIANGULATE_RESULTS`.
- **Security**: `ERASE_REQUEST`, `ERASE_FORCE`, `ERASE_CANCEL`,
  `AUTOERASE_*`, `VIBRATION_*`.
- **Battery**: `BATTERY_SAVER_START`, `BATTERY_SAVER_STOP`,
  `BATTERY_SAVER_STATUS`.
- **Heartbeat**: `HB_ON`, `HB_OFF`, `HB_INTERVAL`.
- **Raw BLE**: `RAW_BLE_ON`, `RAW_BLE_OFF`, `RAW_BLE_STATUS`.
- **Live config**: `CONFIG_TARGETS`, `CONFIG_NODEID`, `CONFIG_RSSI`,
  `CONFIG_CHANNELS`.

For each: one-line purpose, the exact syntax with `:` separators,
optional flags (`FOREVER`, `+PROBE`, `+ALL`), expected `ACK:` reply,
and a worked example.

## See also

- [Operation modes](README.md#operation-modes) — narrative pages for
  the scan-related commands.
- Technical handbook: [Mesh protocol](../tech/protocols/mesh.md) for
  the wire-level view (framing, rate limits, ACK format).
