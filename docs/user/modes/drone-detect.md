# Drone Remote ID detection

Passive capture of FAA / EU / French OpenDroneID broadcasts over
Wi-Fi 2.4 GHz. When a Remote-ID-emitting drone is in the area,
Halberd captures the UAV ID, operator location, and telemetry
without ever transmitting.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- Airspace awareness around a fixed location.
- Drone-incursion alerting (operator location is part of the
  broadcast — you can correlate with property boundaries).
- Recording drone activity logs over time.

## What this page will cover

- `DRONE_START:<seconds>[:FOREVER]` syntax.
- What standards Halberd parses: ASTM F3411 (US Wi-Fi NaN, Bluetooth
  4 + 5), the French "ID-FR" extension (specific OUI
  `0x6a5c35`).
- What's in a detection: UAV ID, operator (pilot) lat/lon when
  broadcast, altitude (geodetic / barometric), speed, heading,
  status flags.
- Bluetooth Remote ID coverage (BLE 4 vs 5 long-range with C5 Coded
  PHY on v5 hardware).
- Output → `/drones.jsonl` and mesh alerts for new UAV IDs.
- Range expectations and antenna considerations.

## Worked example

Detect a hobbyist quad in your neighbourhood, decode the operator
location, build a per-drone activity record.

## See also

- [Triangulation](triangulation.md) for positioning a drone using
  multiple Halberd nodes (RSSI fix, augments the operator-supplied
  drone position).
- [Data formats](../data-formats.md) — the `/drones.jsonl` schema.
