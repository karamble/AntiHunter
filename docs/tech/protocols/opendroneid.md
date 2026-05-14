# OpenDroneID parsing

Halberd decodes drone Remote ID broadcasts using a custom
OpenDroneID parser at
[`Halberd/full/src/opendroneid.{c,h}`](https://github.com/karamble/halberd/blob/main/Halberd/full/src/opendroneid.c).
This page documents what standards Halberd supports, the message
structures it unpacks, and where the boundaries of the current
implementation are.

## Standards supported

| Standard | Carrier | Notes |
|---|---|---|
| **ASTM F3411** | Wi-Fi NaN (Neighbor Awareness Networking) action frames + beacon frames | The FAA-mandated US Remote ID broadcast format. Also adopted by many other regulators. |
| **ASTM F3411** | Bluetooth 4 (BLE legacy advertising) | Same payload format, different carrier. |
| **ASTM F3411** | Bluetooth 5 (BLE long-range Coded PHY) | Same payload, requires C5 (S3 can't reliably scan Coded PHY). |
| **EU implementing acts** | Same as ASTM F3411 | No parser difference. |
| **French "ID-FR" extension** | Wi-Fi beacon frame with vendor-specific IE, OUI `0x6A:5C:35` | Older French regulator format, partially overlapping with ASTM F3411. |

The parser doesn't care which regulator mandated which subset.
it parses the payload it sees and lets the upstream decide what
to do with it.

## Capture path

```
Wi-Fi 2.4 GHz frame  ──┐
                        ├─►  drone_detector.cpp  ──►  opendroneid_decode_*  ──►  ParsedDroneFrame
BLE 4/5 adv frame   ──┘
```

- The S3 puts its Wi-Fi radio into promiscuous mode during
 `DRONE_START`, filters for NaN action frames and beacon frames
 carrying the ASTM OUI / French ID OUI.
- BLE adverts pass through the existing scanner. Ones with the
 ASTM service UUID land in the drone path.
- Each captured frame is handed to `opendroneid_decode_message_pack`
 (for bundled frames) or one of the per-type decoders.

## Message types decoded

ASTM F3411 defines several message types. Halberd decodes all of
the broadcast (non-authenticated) ones:

| # | Name | Halberd unpacks |
|---:|---|---|
| 0 | Basic ID | UAV ID + ID type (serial / CAA / UTM / specific) |
| 1 | Location / Vector | Lat, lon, altitude (geodetic + barometric), speed, heading, vertical speed, status flags |
| 2 | Authentication | Decoded (header + data) but **not verified** (no key infrastructure) |
| 3 | Self-ID | Operator-supplied free-form string (often flight purpose) |
| 4 | System | Operator location (lat, lon), operator altitude, area count, area radius, area ceiling/floor |
| 5 | Operator ID | Registered operator identifier (e.g. EU operator number) |
| 0xF | Message Pack | Multi-message bundle. Halberd unrolls and decodes each contained message |

For each unpacked message, Halberd emits one mesh frame
(`DRONE_HIT`) and appends an entry to `/drones.jsonl` on the SD
card. The drone-detector consolidates messages with the same
UAV ID over a short window before forwarding, so one drone =
one mesh hit per detection cycle even if it sent five message
types.

### Field semantics

The C structs in
[`opendroneid.h`](https://github.com/karamble/halberd/blob/main/Halberd/full/src/opendroneid.h)
match the ASTM byte layout 1:1 and are exhaustively commented in
the source. Highlights:

- **Lat / lon**: signed int32, units = 1e-7 degrees. Multiply by
 `1e-7` to get degrees.
- **Altitudes**: signed int16, units = 0.5 m, offset by 1000 m.
 Decoded value `= raw * 0.5 - 1000` (so raw 0 → -1000 m, raw
 2000 → 0 m, etc.).
- **Speeds**: `Horizontal speed` is unsigned int8 (×0.25 m/s up to
 64 m/s. Otherwise ×0.75 with a +64 offset). `Vertical speed` is
 signed int8 ×0.5 m/s.
- **Heading**: unsigned int8, units = degrees, 0.359.
- **Timestamp**: 16-bit hundredths-of-a-second-into-the-hour
 (UTC). Halberd correlates with the RTC to produce a wallclock
 time.

## Limitations

- **Bluetooth long-range Remote ID** requires BLE 5 Coded PHY.
 Supported only on the v5 carrier via the C5 coprocessor. V4
 hardware sees the BLE 4 variant but misses long-range adverts
 entirely.
- **Authentication messages** are decoded but not cryptographically
 verified. There's no key infrastructure in firmware. Verifying
 a Remote ID auth signature against a registered operator's public
 key is a server-side concern.
- **Encrypted Remote ID** (the optional ASTM Auth message with an
 encrypted payload) is unpacked at the header level but its
 ciphertext is logged as-is for offline analysis.
- **Active probing** of drones is out of scope. Halberd is
 receive-only. We never transmit on Wi-Fi or BLE.

## Drone telemetry vs. RSSI triangulation

A Remote ID broadcast contains the *drone's* location (and often
the *operator's* location), self-reported. Trust depends on the
drone. Non-compliant or spoofing UAVs can lie.

Halberd cross-checks by RSSI-triangulating the drone itself:
multiple nodes capture the same UAV ID, each records its RSSI, and
[Triangulation](././user/modes/triangulation.md) produces an
independent position estimate. A mismatch between reported
position and RSSI fix is suspicious.

## See also

- User handbook: [Drone RID detection](././user/modes/drone-detect.md)
 . Operator-side usage.
- [`Halberd/full/src/opendroneid.h`](https://github.com/karamble/halberd/blob/main/Halberd/full/src/opendroneid.h)
 . Canonical struct definitions, exhaustively commented.
- [Mesh protocol](mesh.md). The `DRONE_HIT` frame format used to
 forward detections.
