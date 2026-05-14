# Data formats

Halberd writes detection results to the local SD card as JSONL
(one JSON object per line). This page documents every log file you'll
find, the fields each record carries, and how to interpret them.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## What this page will cover

- `/probes.jsonl` — probe requests captured during `PROBE_START`. MAC,
  OUI-derived vendor, RSSI, channels, every SSID the device probed
  for, ghost-SSID flag (probed-for but no AP ever responded),
  responding-AP mapping when known, history annotations from
  `/probedb.jsonl`.
- `/probedb.jsonl` — persistent MAC → vendor / first-seen /
  last-seen / session-count database that probe scans annotate
  against.
- `/deauth.jsonl` — captured 802.11 deauth + disassoc frames with
  source / destination / BSSID / reason code / RSSI / channel.
- `/drones.jsonl` — OpenDroneID broadcasts with UAV ID, operator
  location, telemetry (altitude / speed / heading), pilot position
  when reported.
- `/vibrations.jsonl` — tamper events with timestamp, GPS lat/lon
  (if available), pre / post acceleration values from the SW-420.
- `/halberd.log` — system event log (text, not JSON). Boot banner,
  command dispatch, error traces, scan-state transitions.

For each: a real captured-line example, field-by-field meaning,
common interpretation pitfalls (e.g. randomized MACs, hidden SSIDs,
broadcasted-vs-targeted deauth).

## Rotation

Each `.jsonl` rotates at ~1 MB to `_old.jsonl`. Older history is
overwritten — pull rotated files off the SD periodically if long-term
retention matters.

## See also

- [Operation modes](README.md#operation-modes) — each mode's page
  links to the log file it writes.
- Technical handbook for the writer code paths.
