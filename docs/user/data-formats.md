# Data formats

Halberd writes detection results to the local SD card as JSONL
(one JSON object per line) plus a plain-text system log. This page
documents every log file you'll find, the fields each record
carries, and how to interpret them.

## File list

| Path | Format | Source |
|------|--------|--------|
| `/probes.jsonl` | JSONL | Probe-request scans |
| `/probedb.jsonl` | JSONL | Persistent probe-history database |
| `/deauth.jsonl` | JSONL | Deauth attack detection |
| `/drones.jsonl` | JSONL | OpenDroneID drone detection |
| `/vibrations.jsonl` | JSONL | SW-420 tamper events |
| `/halberd.log` | text | System event log |

Each JSONL file rotates at approximately 1 MB to `_old.jsonl`.
Older history is overwritten. Pull rotated files off the SD
periodically if you need long-term retention.

## `/probes.jsonl`

Probe requests captured during `PROBE_START` (and during a device
scan with the `+PROBE` flag).

Example line:

```json
{"t":1747237891,"mac":"AA:BB:CC:DD:EE:FF","rssi":-58,"ch":6,"cnt":12,"rand":false,"v":"Apple","hit":true,"ss":["MyWifi","CafeFree","~HomeOffice"],"ap":"22:33:44:55:66:77","apssid":"MyWifi"}
```

Fields:

| Field | Type | Meaning |
|-------|------|---------|
| `t` | int | Unix epoch seconds of last seen |
| `mac` | string | Probing device MAC |
| `rssi` | int | Last RSSI in dBm |
| `ch` | int | Last channel observed |
| `cnt` | int | Total probe requests from this MAC |
| `rand` | bool | true if locally administered bit set (randomized) |
| `v` | string | OUI vendor lookup, or "Unknown" |
| `hit` | bool | Matched against `CONFIG_TARGETS` |
| `dst` | bool | Caught as destination of someone else's probe |
| `ss` | array | SSIDs this device probed for. `~` prefix marks ghost SSIDs (probed but never seen replying) |
| `ap` | string | BSSID of an AP that responded to this device |
| `apssid` | string | SSID announced in that response |

Up to 4 SSIDs are tracked per device. Ghost SSIDs are the most
useful field for fingerprinting. A device probing for `~HomeOffice`
is telling you the name of its home network even when that network
is nowhere near.

## `/probedb.jsonl`

Persistent MAC to vendor / first-seen / last-seen database. Probe
scans annotate against it. Survives reboots.

Example:

```json
{"mac":"AA:BB:CC:DD:EE:FF","t":120,"f":1746500001,"l":1747237891,"s":4,"rd":false,"v":"Apple"}
```

Fields:

| Field | Type | Meaning |
|-------|------|---------|
| `mac` | string | Device MAC |
| `t` | int | Total times this MAC has been seen across all sessions |
| `f` | int | First-seen Unix epoch |
| `l` | int | Last-seen Unix epoch |
| `s` | int | Session count (distinct scan sessions this MAC appeared in) |
| `rd` | bool | Randomized MAC flag |
| `v` | string | Vendor |

A probe scan's results carry these fields too when there's a match,
under the `hist*` prefix in the firmware (e.g. `histTotalSeen`,
`histFirstEpoch`).

## `/deauth.jsonl`

Captured 802.11 deauth and disassoc frames.

Example:

```json
{"t":1747237900,"src":"11:22:33:44:55:66","dst":"AA:BB:CC:DD:EE:FF","bssid":"22:33:44:55:66:77","reason":7,"rssi":-42,"ch":6,"type":"deauth"}
```

Fields:

| Field | Type | Meaning |
|-------|------|---------|
| `t` | int | Unix epoch seconds |
| `src` | string | Sender MAC |
| `dst` | string | Target MAC (or `FF:FF:FF:FF:FF:FF` for broadcast) |
| `bssid` | string | AP being spoofed |
| `reason` | int | 802.11 reason code |
| `rssi` | int | RSSI in dBm |
| `ch` | int | Channel |
| `type` | string | `deauth` or `disassoc` |

Broadcast deauths from one source repeated rapidly are a strong
attack signature. Single deauths to one client are typically
legitimate (client voluntarily leaving an AP).

## `/drones.jsonl`

OpenDroneID broadcasts decoded by `drone_detector.cpp`.

Example:

```json
{"t":1747237950,"uav":"1581F5KAS1234XYZ","lat":48.137154,"lon":11.576124,"alt":120,"speed":4.5,"heading":190,"pilot_lat":48.135001,"pilot_lon":11.572341,"id_type":1,"rssi":-72}
```

Fields (the most useful subset. ASTM F3411 defines several
more):

| Field | Type | Meaning |
|-------|------|---------|
| `t` | int | Unix epoch seconds |
| `uav` | string | UAV identifier (serial / CAA / UTM / specific) |
| `lat`, `lon` | float | Drone location, decimal degrees |
| `alt` | int | Altitude in meters above mean sea level |
| `speed` | float | Horizontal speed in m/s |
| `heading` | int | Course over ground in degrees |
| `pilot_lat`, `pilot_lon` | float | Operator location when reported |
| `id_type` | int | ASTM F3411 ID type code |
| `rssi` | int | Capture RSSI |

The drone's reported position is self-asserted. A spoofing or
non-compliant UAV can lie. RSSI-triangulating the drone from
multiple Halberd nodes gives an independent estimate. See
[triangulation](modes/triangulation.md).

## `/vibrations.jsonl`

Tamper events from the SW-420 sensor.

Example:

```json
{"t":1747237999,"count":7,"lat":48.137154,"lon":11.576124,"reason":"vib"}
```

Fields:

| Field | Type | Meaning |
|-------|------|---------|
| `t` | int | Unix epoch seconds |
| `count` | int | Vibration count within the rolling window |
| `lat`, `lon` | float | GPS at time of event (if available) |
| `reason` | string | `vib` for vibration, may be extended later |

Used together with the auto-erase policy. See
[security](security.md).

## `/halberd.log`

Plain-text system log. Boot banner, command dispatch, error
traces, scan-state transitions. Useful for postmortem on a node
that misbehaved.

Each line is prefixed with a timestamp:

```
2026-05-14 18:23:01 [SCANNER] PROBE_START mode=2 secs=300 forever=0
2026-05-14 18:23:01 [SCANNER] Probe DB loaded: 142 known devices
2026-05-14 18:23:01 [MESH] HB-A1B2: PROBE_ACK:STARTED
```

No JSON schema. Read it like any log file.

## Reading the files

Three ways.

- **Web UI Data Explorer** (full only). Browse from the local
 dashboard.
- **REST API**. `GET /api/probes.jsonl`, `GET /api/deauth.jsonl`,
 etc. Streams the file straight from the SD card.
- **Pull the SD card**. Standard FAT32. Mount it on any host.

## See also

- [Operation modes](README.md#operation-modes) for the mode that
 writes each file.
- Technical handbook: [REST API reference](./tech/api-rest.md)
 for the `/api/*.jsonl` endpoints.
