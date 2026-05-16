# Mesh protocol. Halberd ↔ diginode-cc

Halberd nodes talk to each other and to the `diginode-cc` fleet
server over Meshtastic. The wire format is **line-oriented text
frames** on the Meshtastic TEXTMSG channel. Readable, easy to
parse, dispatched on the first token. This page is the integration
reference for anyone implementing the protocol on a non-Halberd
peer.

## Transport

| | |
|---|---|
| Carrier | Meshtastic |
| Channel | primary (TEXTMSG) |
| Frame type | text |
| Max payload | `MAX_MESH_SIZE` ≈ 220 bytes (well below Meshtastic's 237 B cap, leaves margin for headers) |
| LoRa side | runs on the Heltec V3 sidekick, not the Halberd MCU |
| Region | configured at flash time on the Heltec (`HELTEC_REGION` default `EU_868`) |
| Halberd ↔ Heltec | UART1, 115200 baud, S3 to Heltec |

The Halberd S3 emits text frames via `sendToSerial1(msg, true)` to
the Heltec, which forwards them on the mesh. Receives travel the
same path in reverse and land in the S3's dispatcher.

There's no chunking today. Reply frames must fit in a single
`MAX_MESH_SIZE` packet. If a result is too large, the firmware
trims rather than splitting.

## Frame format

Every Halberd-originated frame begins with the originating node's
ID and a colon-separated payload:

```
<NODE_ID>: <COMMAND_OR_KIND>:<args>
```

Examples:

```
HB-NODE1: STATUS_ACK:ALIVE Uptime:00:14:23 GPS:48.137154,11.576124 Mode:WiFi+BLE
HB-NODE1: HIT MAC=AA:BB:CC:DD:EE:FF Vendor=Apple RSSI=-58 CH=6
HB-NODE1: PROBE_HIT MAC=AA:BB:... Vendor=Unknown RSSI=-72 CH=11 SSID="HomeWifi" GHOST
HB-NODE1: HEARTBEAT Time:2026-05-14_18:23:01 Temp:42.3C GPS:48.137,11.576
```

Inbound commands target a specific node by ID (or are broadcast):

```
SCAN_START:0:60
PROBE_START:0:60:FOREVER:+ALL
TRIANGULATE_START:AA:BB:CC:DD:EE:FF:120:indoor
```

The dispatcher splits on `:` and routes by the first token. Unknown
commands are silently dropped. Known commands return a one-line
ACK (`<NODE>: <COMMAND>_ACK:<STATUS>`) before doing any work.

## ACK envelope

```
<NODE_ID>: <COMMAND>_ACK:<STATUS>
```

`STATUS` is one of:

- **`STARTED`**. The command was accepted and a worker task has
 been spawned. Subsequent result frames follow.
- **`STOPPED`**. The command was a stop / cancel and the running
 job was halted.
- **`BUSY`**. Another scan / detection task was already running.
 the new command was rejected. Caller retries later.
- **`INVALID`**. The command parsed but the arguments were
 out-of-range or otherwise unusable.
- **`OK`**. For queries that return inline data, the value is
 in the same frame.
- **`ERR:<reason>`**. Generic failure with a short hint.

Every long-running command (scans, detection modes,
triangulation) emits exactly one ACK. Results stream as separate
frames keyed by `KIND` (see below). The end of a finite-duration
run is marked by a `<COMMAND>_DONE:<summary>` frame.

## Command registry

All commands accepted on the mesh. Each is parsed in
`network.cpp::handleMeshMessage` (or its equivalent dispatcher).

| Command | Direction | Purpose |
|---|---|---|
| `STATUS` | request | Health check, GPS, mode, uptime |
| `STOP` | request | Cancel any running scan / detection task |
| `SCAN_START:<mode>:<secs>[:<channels>][:FOREVER]` | request | Target hunt (MAC / SSID watchlist) |
| `DEVICE_SCAN_START:<mode>:<secs>[:FOREVER[:+PROBE]]` | request | Enumerate all devices |
| `PROBE_START:<mode>:<secs>[:FOREVER[:+ALL]]` | request | Probe-request intelligence |
| `BASELINE_START:<secs>[:FOREVER]` | request | Learn-then-monitor anomaly detector |
| `DRONE_START:<secs>[:FOREVER]` | request | OpenDroneID drone detection |
| `DEAUTH_START:<secs>[:FOREVER]` | request | 802.11 deauth/disassoc attack detector |
| `RANDOMIZATION_START:<mode>:<secs>[:FOREVER]` | request | MAC-randomization correlation |
| `TRIANGULATE_START:<target>:<duration>[:<rfEnv>[:<wifiPwr>:<blePwr>]]` | request | Multi-node RSSI fix |
| `TRIANGULATE_STOP` | request | End an in-flight triangulation |
| `TRIANGULATE_RESULTS` | request | Re-emit the last fix |
| `BATTERY_SAVER_START:<minutes>` | request | Engage radio-duty-cycle mode |
| `BATTERY_SAVER_STOP` | request | Disengage |
| `BATTERY_SAVER_STATUS` | request | Query state |
| `AUTOERASE_ENABLE` / `_DISABLE` / `_STATUS` | request | Tamper-triggered wipe policy |
| `VIBRATION_ON` / `_OFF` / `_STATUS` | request | SW-420 tamper sensing |
| `HB_ON` / `_OFF` / `_INTERVAL:<minutes>` | request | Periodic heartbeat broadcast |
| `RAW_BLE_ON` / `_OFF` / `_STATUS` | request | Raw BLE adv frame broadcast over mesh |
| `ERASE_REQUEST` | request | Initiate secure-erase challenge |
| `ERASE_FORCE:<token>` | request | Confirm + execute secure erase |
| `ERASE_CANCEL` | request | Abort an in-flight erase request |
| `CONFIG_TARGETS:<mac1,mac2,…>` | request | Set / replace target watchlist |
| `CONFIG_NODEID:<id>` | request | Change the node's mesh ID |
| `CONFIG_RSSI:<dBm>` | request | Set the RSSI cutoff threshold |
| `CONFIG_CHANNELS:<list>` | request | Set the Wi-Fi channel list |
| `SETTIME:<epoch>` | request | Push host's Unix time to node RTC |

Argument grammar:

- `mode` ∈ `{0=Wi-Fi, 1=BLE, 2=both}`.
- `secs` is in seconds. `FOREVER` runs until `STOP`.
- `channels` is a comma-separated channel list (e.g.
 `1,6,11,36,40`).
- `target` is a MAC address in `AA:BB:CC:DD:EE:FF` form.
- `rfEnv` ∈ `{open, suburban, indoor, indoor-dense, industrial}`.

## Result kinds

Long-running commands stream results as separate frames:

| KIND | Source command | Example payload |
|---|---|---|
| `HIT` | `SCAN_START` | `MAC=AA:... RSSI=-58 CH=6 SSID="..."` |
| `PROBE_HIT` | `PROBE_START` | `MAC=AA:... RSSI=-72 CH=11 SSID="HomeWifi" GHOST` |
| `DEAUTH_HIT` | `DEAUTH_START` | `SRC=AA:... DST=FF:FF:... BSSID=11:22:... CH=6 REASON=7` |
| `DRONE_HIT` | `DRONE_START` | `UAV=1234XYZ Lat=48.137 Lon=11.576 Alt=120m Pilot=48.135,11.572` |
| `ANOMALY` | `BASELINE_START` | `KIND=NEW MAC=AA:... Vendor=...` |
| `HEARTBEAT` | `HB_ON` | `Time:... Temp:... GPS:...` |
| `TRIANGULATE_RESULT` | `TRIANGULATE_START` | `Target=AA:... Lat=48.137 Lon=11.576 Conf=92% Err=±5m` |

All result frames begin with `<NODE_ID>: <KIND> ...`.

## Rate limiting

Floods of hits would saturate the mesh. Halberd applies per-target
dedup:

- **`HIT`**: 3 s cooldown per target MAC.
- **`PROBE_HIT`**: 60 s cooldown per (MAC, SSID) pair.
- **`ANOMALY`**: per-event, no dedup (anomalies should be rare).
- **`DEAUTH_HIT`**: rate-limited by aggregation into per-attacker
 summaries during floods.
- **`HEARTBEAT`**: emits at `HB_INTERVAL` minutes.

Cooldowns are per-node. Multiple nodes detecting the same target
won't be deduplicated automatically across the mesh. The
diginode-cc server is expected to dedupe on its side.

## Implementing a non-Halberd peer

To talk to Halberd nodes from your own server / sensor:

1. **Stand up a Meshtastic node** on the same channel as the
 Halberd Heltec sidekicks. Same region, same modem preset, same
 PSK if the channel is encrypted.
2. **Listen** for `<NODE_ID>: <KIND> ...` text frames. Parse the
 first colon-pair to extract the originating node ID, then split
 the rest by whitespace into key=value tokens (or treat each
 `KIND` as its own grammar. See above).
3. **Send commands** as text frames addressed to the target node's
 ID. Wait for the matching `<COMMAND>_ACK`.
4. **Dedupe**. The same hit arriving from multiple nodes is normal.
 that's how triangulation works. Your server collapses across
 nodes. Each individual node only dedupes against itself.

The reference parser on the server side lives in
`karamble/diginode-cc`. The Go backend has the canonical
key=value tokeniser and the schema mapping into Postgres.

## Heartbeat format

When `HB_ON` is active, every `HB_INTERVAL` minutes the node emits:

```
<NODE_ID>: HEARTBEAT Time:<YYYY-MM-DD>_<HH:MM:SS> Temp:<C> GPS:<lat>,<lon> Mode:<scan-mode>
```

Heartbeats are how the fleet server detects "node X is alive". A
missing heartbeat for ~2× the interval means the node is offline,
out of range, or its battery is gone.

## See also

- User handbook: [Mesh command reference](././user/commands.md)
 . The same registry from an operator perspective.
- [REST API reference](./api-rest.md). The parallel HTTP
 interface on `halberd-full`.
- [Adding a mesh command](./extending/new-mesh-command.md). How
 to extend this protocol.
