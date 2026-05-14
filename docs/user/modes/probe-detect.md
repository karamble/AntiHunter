# Probe-request detection

Capture 802.11 probe requests + responses, build a per-device profile
from the SSIDs each client probes for, and surface "ghost" SSIDs
(probed for but never seen replying nearby — usually the client's
home / office network). On v5 hardware, the C5 coprocessor extends
this to 5 GHz channels as well.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- Identifying clients by their preferred-network list, not just
  their MAC.
- Tracking specific people / devices that move between locations
  (their SSID list is a fingerprint).
- Detecting MAC randomization (probes with random MACs but
  consistent SSID sets often belong to the same device).

## What this page will cover

- `PROBE_START:<mode>:<seconds>[:FOREVER][:+ALL]` syntax.
- The `+ALL` flag — broadcast every probe hit to the mesh
  (subject to the 60 s per-MAC+SSID dedup) versus only target-
  matching probes.
- Mode 0 (Wi-Fi) / 1 (BLE — thin) / 2 (both).
- What the results table contains: MAC, vendor, RSSI, channel,
  randomization flag, SSID list, ghost-SSID list, responding-AP
  mapping, history annotations from `/probedb.jsonl`.
- The 5 GHz extension on v5 hardware: how the C5 contributes,
  cross-band MAC merging, what to expect in the channel column.
- Output → `/probes.jsonl` and persistent annotations in
  `/probedb.jsonl`.

## Worked example

Hunt a target's preferred-network list, watch ghost SSIDs accumulate,
identify the home-network SSID, correlate across multiple visits.

## See also

- [Target hunt](target-hunt.md) when you already know the MAC or
  SSID to hunt for.
- [MAC randomization correlation](randomization.md) for the
  fingerprint-based deduplication beyond what probe detection
  alone provides.
- [Data formats](../data-formats.md) — the `/probes.jsonl` and
  `/probedb.jsonl` schemas.
