# Triangulation

Get an actual position fix on a target using multiple Halberd nodes
with synchronised RSSI measurements and GPS-locked timestamps. Three
or more nodes give a 2D fix. More nodes improve accuracy.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- "Where is this MAC?" rather than "is this MAC nearby?"
- Pinpointing the operator of a detected drone.
- Locating an anomalous device that a [Baseline](baseline.md) alerted
 on.

## What this page will cover

- The hardware requirements: at least 3 nodes with known positions
 (GPS-locked or surveyed), tight clock sync (the GPS-disciplined
 RTC on each Halberd handles this).
- `TRIANGULATE_START:<target>:<duration>[:<rfEnv>[:<wifiPwr>:<blePwr>]]`
 syntax. What each parameter does.
- The `rfEnv` parameter: free space, urban, indoor. Path-loss model
 calibration.
- `TRIANGULATE_STOP` and `TRIANGULATE_RESULTS`. Orchestration.
- Reading the result: estimated lat/lon, confidence circle, per-node
 RSSI contributions.
- Time-of-fix consistency: how the C5's GPS feed keeps the RSSI
 samples aligned across nodes.
- Practical accuracy expectations (RSSI is noisy. Meters in
 best-case open terrain, tens of meters indoor).

## Worked example

Four nodes in a parking lot triangulate a target phone with a known
MAC. The fix lands within ~5 m of the actual position.

## See also

- [Target hunt](target-hunt.md) as the prior step (you usually
 triangulate something a hunt has already detected).
- [Drone Remote ID detection](drone-detect.md). Drone telemetry
 carries its own operator location, but you can RSSI-triangulate
 the drone itself for independent verification.
