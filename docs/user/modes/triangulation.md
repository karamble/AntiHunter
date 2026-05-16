# Triangulation

Get a position fix on a target using multiple Halberd nodes with
synchronised RSSI measurements and GPS-locked timestamps. Three
or more nodes give a 2D fix. More nodes improve accuracy.

## When to use it

- "Where is this MAC?" rather than "is this MAC nearby?".
- Pinpointing the operator of a detected drone.
- Locating a device that a [baseline](baseline.md) alerted on.
- Independent verification of a drone's self-reported position.

Not the right mode for:

- A single node. Triangulation needs 3+ nodes with known
 positions.
- Devices not currently broadcasting. The target must transmit
 during the scan window so each node can sample its RSSI.

## What you need

- **3 or more Halberd nodes** with known locations (GPS-locked
 or surveyed).
- **Tight clock sync** between nodes (the GPS-disciplined
 DS3231 RTC on each Halberd handles this automatically).
- **A mesh link between nodes** that lets them exchange data
 frames during the fix.
- **A target MAC** that's actively transmitting on Wi-Fi or
 BLE.

## Starting a fix

Drive from one node, typically the one closest to the
suspected target location.

```
@HB01 TRIANGULATE_START:<target>:<duration>[:<rfEnv>[:<wifiPwr>:<blePwr>]]
```

| Field | Values |
|---|---|
| `target` | Target MAC (e.g. `AA:BB:CC:DD:EE:FF`) |
| `duration` | Fix window in seconds (typical 60 to 300) |
| `rfEnv` | RF environment code (see below). Optional, default 2 (indoor) |
| `wifiPwr` | Wi-Fi distance multiplier 0.1 to 5.0. Optional |
| `blePwr` | BLE distance multiplier 0.1 to 5.0. Optional |

`rfEnv` codes:

| Code | Environment | Wi-Fi n | BLE n |
|---:|---|---|---|
| 0 | Open sky | 2.0 | 2.0 |
| 1 | Suburban | 2.7 | 2.5 |
| 2 | Indoor (default) | 3.2 | 2.9 |
| 3 | Indoor dense | 4.0 | 3.5 |
| 4 | Industrial | 4.8 | 4.0 |

`n` is the path-loss exponent used by the distance estimator.
Higher `n` means signal falls off faster (more obstructions).
Pick the closest match to your deployment.

Stop early with `@HB01 TRIANGULATE_STOP` or wait for `duration`
to elapse.

## How a fix unfolds

1. The coordinator node broadcasts the target and parameters to
 the mesh.
2. Each participating node samples the target's RSSI repeatedly
 over the duration, tagging each sample with GPS lat/lon and a
 GPS-disciplined timestamp.
3. Per-sample data flows back to the coordinator as `T_D` mesh
 frames:

   ```
   HB-CD34: T_D: AA:BB:CC:DD:EE:FF RSSI:-58 Type:WiFi GPS=48.137,11.576 HDOP=1.5
   ```

4. After duration, the coordinator runs weighted trilateration
 with Kalman filtering and emits the result as `T_F`:

   ```
   HB-A1B2: T_F: MAC=AA:BB:CC:DD:EE:FF GPS=48.137,11.576 CONF=85.5 UNC=12.3
   ```

5. A `T_C` complete frame closes the fix with a Google Maps
 link:

   ```
   HB-A1B2: T_C: MAC=AA:BB:CC:DD:EE:FF Nodes=4 https://maps.google.com/?q=48.137,11.576
   ```

`CONF` is the confidence percentage. `UNC` is the uncertainty
in meters (the radius of the confidence circle).

## Reading the result

Open the Google Maps link, or pull the result via the web UI's
**Triangulation** page. The fix shows:

- Estimated lat / lon.
- Per-node contribution table.
- Confidence circle drawn on the map.

## Tuning for accuracy

- **More nodes**. 3 nodes give a fix, 4+ tighten it
 significantly.
- **Correct `rfEnv`**. The single biggest factor. An indoor
 fix run with `rfEnv=0` will be wildly wrong.
- **Manual power multipliers**. If you know one node has a
 particularly strong antenna or sits in a clear position, bump
 its `wifiPwr` / `blePwr` accordingly.
- **Above -80 dBm**. Weak targets produce noisy fixes. For BLE,
 RSSI above -80 dBm at every node is the sweet spot.
- **Use a Heltec V3 for the mesh sidekick**. The T114's smaller
 buffer causes latency during multi-node coordination.

## Practical accuracy

- **Open terrain, 4+ nodes**: 3 to 10 meters.
- **Suburban**: 5 to 20 meters.
- **Indoor**: tens of meters typically.
- **Industrial / heavy obstruction**: highly variable.

RSSI is noisy. Don't expect GPS-level precision. The fix tells
you "this device is approximately here", not "this device is
exactly at coordinates X".

## Worked example

Four nodes deployed around a perimeter. A target phone with
MAC `AA:BB:CC:DD:EE:FF` is suspected to be inside.

```
@HB01 TRIANGULATE_START:AA:BB:CC:DD:EE:FF:120:2:1.0:1.0
```

120-second fix, indoor environment, default power multipliers.
The coordinator emits incoming T_D frames on the mesh as nodes
report samples. After 2 minutes:

```
HB-A1B2: T_F: MAC=AA:BB:CC:DD:EE:FF GPS=48.13721,11.57612 CONF=89.2 UNC=7.4
HB-A1B2: T_C: MAC=AA:BB:CC:DD:EE:FF Nodes=4 https://maps.google.com/?q=48.13721,11.57612
```

7 meters uncertainty at 89% confidence. Open the maps link to
see the fix.

If you want to retrieve the last result later:

```
@HB01 TRIANGULATE_RESULTS
```

## See also

- [Target hunt](target-hunt.md). Confirm presence before
 starting a fix.
- [Drone Remote ID detection](drone-detect.md). Drones report
 their own position. Triangulation is the independent check.
- [Mesh command reference](./commands.md).
