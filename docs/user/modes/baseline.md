# Baseline anomaly detection

Learn what "normal" looks like in a location over a period of time,
then alert when something deviates: a new MAC that shouldn't be
there, an expected MAC that's missing, RSSI shifts indicating
movement.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- Unattended monitoring of a fixed location.
- Perimeter alerting (anything new arrives / known thing leaves).
- Pre-event sweep + during-event monitoring.

## What this page will cover

- `BASELINE_START:<seconds>[:FOREVER]`. Duration vs FOREVER mode.
- The state machine: **learn phase** (build the baseline) →
 **monitor phase** (detect anomalies against it).
- How long to learn for, in practice (rule of thumb: 3–10× the
 longest normal-presence cycle in the environment).
- RSSI anomaly thresholds: what counts as "moved", what doesn't.
- Output: anomaly alerts on the mesh, baseline DB persisted to SD,
 stats reachable via web UI / API on `halberd-full`.
- Resetting the baseline if the environment changes.
- Tiering: RAM-resident baseline of 200–500 devices, SD overflow.

## Worked example

48-hour learn phase in a small office, then monitor mode catches a
new device on day 3.

## See also

- [Device scan](device-scan.md) for the underlying enumeration that
 feeds the baseline.
- [Triangulation](triangulation.md) for positioning new arrivals
 detected by baseline.
