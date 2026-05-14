# OpenDroneID parsing

Halberd decodes Remote ID broadcasts from drones using a custom
OpenDroneID parser (`Halberd/full/src/opendroneid.{c,h}`). This page
documents what standards Halberd supports, the message structures it
unpacks, and the limitations of the current implementation.

> 🚧 **Stub page.** Content lands in a follow-up commit, sourced
> from the `opendroneid.{c,h}` headers and the
> drone-detector code paths.

## What this page will cover

### Standards supported

- **ASTM F3411** — the FAA-mandated Remote ID standard. Wi-Fi NaN
  (Neighbor Awareness Networking) carrier, Bluetooth 4 (BLE
  legacy), and Bluetooth 5 (BLE long-range) carriers.
- **EU implementing acts** — same payload as ASTM F3411, different
  regulatory framing. No parser difference.
- **French "ID-FR" extension** — French regulator-specific
  identifier with OUI `0x6a5c35`.

### Message types decoded

- Basic ID (UAV registration / serial).
- Location / Vector (lat, lon, altitude, speed, heading, status).
- Authentication.
- Self-ID (operator-supplied free-form string, often the flight
  purpose).
- System (operator location).
- Operator ID.
- Message Pack (multi-message bundling).

For each: the on-air struct, the C struct Halberd unpacks it into,
the field semantics, the unit conversions Halberd applies.

### Limitations

- Bluetooth long-range RID requires BLE 5 Coded PHY — supported on
  the v5 hardware via the C5 only.
- Encrypted Auth messages: decoded but not verified (no key
  infrastructure in firmware).
- Drone direction-of-flight independent verification (RSSI-based
  triangulation) is documented separately under
  [Triangulation](../../user/modes/triangulation.md).

## See also

- User handbook: [Drone RID detection](../../user/modes/drone-detect.md).
- [`Halberd/full/src/opendroneid.h`](https://github.com/karamble/halberd/blob/main/Halberd/full/src/opendroneid.h) for the C-level struct definitions.
