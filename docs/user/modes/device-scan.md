# Device scan

Enumerate every Wi-Fi AP, Wi-Fi client (via probe requests), and BLE
advertiser in range. The right mode when the question is **"what's
out there?"** rather than "is X here?".

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- Site survey before deploying nodes.
- Building an initial picture of an unfamiliar environment.
- Feeding the device list into a baseline (see
 [Baseline](baseline.md)).

## What this page will cover

- `DEVICE_SCAN_START:<mode>:<seconds>[:FOREVER[:+PROBE]]` syntax.
- The `+PROBE` flag. When to include probe-request capture and what
 it adds (SSIDs probed for, MAC randomization hints).
- Output format: device list grouped by MAC, RSSI, channel, vendor,
 first-seen / last-seen.
- Memory limits: how many distinct devices the node tracks before
 it rotates the oldest out.
- SD logging behaviour during a device scan.

## Worked example

Run a 5-minute device scan, dump the results, identify the densest
APs and the unknown clients.

## See also

- [Probe-request detection](probe-detect.md) for a probe-only
 variant with richer fingerprinting.
- [Baseline](baseline.md) to turn this static snapshot into ongoing
 anomaly detection.
