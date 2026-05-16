# Drone Remote ID detection

Passive capture of FAA / EU / French OpenDroneID broadcasts. When
a Remote-ID-emitting drone is in the area, Halberd captures the
UAV ID, operator location, and telemetry. The node never
transmits.

## When to use it

- Airspace awareness around a fixed location.
- Drone-incursion alerting at sensitive sites (the operator
 location is part of the broadcast, so you can correlate with
 property boundaries).
- Recording drone activity over time.
- Combining with [triangulation](triangulation.md) for
 independent verification of the drone's self-reported
 position.

## What's detected

| Standard | Carrier | Halberd |
|---|---|---|
| ASTM F3411 | Wi-Fi NaN (Neighbor Awareness Networking) | v4 + v5 |
| ASTM F3411 | Bluetooth 4 advertising | v4 + v5 |
| ASTM F3411 | Bluetooth 5 long-range (Coded PHY) | **v5 only** (C5 coprocessor) |
| EU implementing acts | same as ASTM F3411 | v4 + v5 |
| French ID-FR | Wi-Fi beacon, OUI `0x6A:5C:35` | v4 + v5 |

Non-compliant or pre-mandate drones won't show up. Most
consumer drones from 2022 onward broadcast Remote ID.

## Starting a scan

```
@HB01 DRONE_START:<seconds>[:FOREVER]
```

Example:

```
@ALL DRONE_START:1800                   # 30 minutes
@HB01 DRONE_START:0:FOREVER             # forever
```

Stop with `@HB01 STOP`.

## Output

Per-detection alert on the mesh:

```
HB-A1B2: DRONE_HIT UAV=1581F5KAS1234XYZ Lat=48.137 Lon=11.576 Alt=120m Pilot=48.135,11.572
```

And a JSONL record in `/drones.jsonl`:

```json
{"t":1747237950,"uav":"1581F5KAS1234XYZ","lat":48.137154,"lon":11.576124,"alt":120,"speed":4.5,"heading":190,"pilot_lat":48.135001,"pilot_lon":11.572341,"id_type":1,"rssi":-72}
```

Fields:

- `uav` is the broadcasted UAV identifier (serial number, CAA
 ID, UTM ID, or specific session ID depending on `id_type`).
- `lat` / `lon` / `alt` is the drone's self-reported position.
- `speed` / `heading` is current motion.
- `pilot_lat` / `pilot_lon` is the operator's location when
 the drone broadcasts a System message.
- `rssi` is the signal strength of the capture.

See [data formats](./data-formats.md) for the full schema.

## Range and antenna placement

Remote ID broadcasts are low-power and obstacle-sensitive.

- Wi-Fi NaN: typical range a few hundred meters in open
 terrain, much less indoors.
- Bluetooth 4: similar to Wi-Fi NaN.
- Bluetooth 5 Coded PHY: significantly better range, often
 500 m+ in line-of-sight.

Make sure the chassis Wi-Fi antenna is vertical and clear of
obstructions. The C5 has its own antenna connector for 5 GHz
and 802.15.4, but Remote ID rides on 2.4 GHz Wi-Fi which uses
the S3's antenna on both hardware revisions.

## Self-reported vs measured position

The drone's broadcast contains its position. Trust depends on
the drone. A non-compliant or spoofing UAV can lie.

Halberd cross-checks by RSSI-triangulating the drone from
multiple Halberd nodes. The same UAV ID seen by 3+ nodes
produces an independent position estimate from the
[triangulation](triangulation.md) mode. A significant
discrepancy between reported and triangulated position is
suspicious.

## Worked example

A sensitive site wants airspace awareness.

```
@ALL DRONE_START:0:FOREVER
```

All nodes are now passively listening for Remote ID. When a
drone enters airspace:

```
HB-A1B2: DRONE_HIT UAV=ABC123 Lat=48.137 Lon=11.576 Alt=80m Pilot=48.139,11.578
HB-CD34: DRONE_HIT UAV=ABC123 Lat=48.137 Lon=11.576 Alt=80m Pilot=48.139,11.578
```

Both nodes see the same UAV with the same telemetry, confirming
the broadcast. If you want a position fix independent of the
drone's own GPS:

```
@HB01 TRIANGULATE_START:ABC123:120:0:1.0:1.0
```

(See [triangulation](triangulation.md) for the syntax.)

## See also

- [Triangulation](triangulation.md). Independent RSSI position
 fix for drones.
- [Data formats](./data-formats.md). `/drones.jsonl` schema.
- Technical handbook:
 [OpenDroneID parsing](./tech/protocols/opendroneid.md).
