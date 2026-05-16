# Mesh command reference

Every command a Halberd node accepts over Meshtastic, grouped by
use case. For the wire-level format (envelope, ACK statuses, result
KINDs, rate limits), see the
[technical handbook's mesh protocol page](./tech/protocols/mesh.md).

## Addressing

All commands travel on the Meshtastic TEXTMSG channel. Address with:

- `@ALL COMMAND` to broadcast to every reachable node.
- `@HB01 COMMAND` to target a specific node by ID.

Node IDs are 2-5 alphanumeric characters (A-Z, 0-9), `HB-`
prefixed for current firmware. Legacy `AH-` prefixes (from
AntiHunter) still work until a node is re-flashed.

## Core

| Command | Description | Example |
|---------|-------------|---------|
| `STATUS` | Mode, scan state, hits, temp, uptime, GPS | `@ALL STATUS` |
| `STOP` | Stop any running scan or detection task | `@ALL STOP` |
| `SETTIME:<epoch>` | Push Unix time to the node RTC | `@HB01 SETTIME:1747234567` |

## Live configuration

| Command | Parameters | Example |
|---------|------------|---------|
| `CONFIG_TARGETS` | Pipe-delimited MACs, OUI prefixes, or SSIDs | `@ALL CONFIG_TARGETS:AA:BB:CC:DD:EE:FF\|11:22:33\|MyNetwork` |
| `CONFIG_NODEID` | 2-5 alphanumeric ID | `@HB01 CONFIG_NODEID:HB02` |
| `CONFIG_RSSI` | Threshold (-128 to -10) | `@ALL CONFIG_RSSI:-80` |
| `CONFIG_CHANNELS` | Comma-separated channels | `@ALL CONFIG_CHANNELS:1,6,11` |

Configuration changes persist immediately to NVS. They survive
reboots and firmware variant swaps.

## Find a specific device

You know the MAC, OUI, or SSID you're looking for.

| Command | Parameters | Example |
|---------|------------|---------|
| `SCAN_START` | `mode:secs:channels[:FOREVER]` | `@ALL SCAN_START:2:300:1,6,11` |
| `STOP` | None | `@ALL STOP` |

`mode`: 0 = WiFi, 1 = BLE, 2 = both. `channels`: comma-separated
Wi-Fi channels to scan (default uses the saved channel list).
`FOREVER` runs until `STOP`. Set targets first with
`CONFIG_TARGETS`.

See [target hunt](modes/target-hunt.md) for the full workflow.

## Enumerate what's around

You don't know what's there yet. Cast a wide net.

| Command | Parameters | Example |
|---------|------------|---------|
| `DEVICE_SCAN_START` | `mode:secs[:FOREVER[:+PROBE]]` | `@ALL DEVICE_SCAN_START:2:300:+PROBE` |
| `STOP` | None | `@ALL STOP` |

The `+PROBE` flag piggybacks probe-request capture onto the device
scan, populating the probe database alongside normal discovery.

See [device scan](modes/device-scan.md).

## Probe-request intelligence

Build per-device profiles from the SSIDs each client probes for.

| Command | Parameters | Example |
|---------|------------|---------|
| `PROBE_START` | `mode:secs[:FOREVER][:+ALL]` | `@ALL PROBE_START:2:300:+ALL` |
| `PROBE_STOP` | None | `@ALL PROBE_STOP` |

`+ALL` broadcasts every probe to the mesh, not just target
matches. Probe hits dedup at 60 seconds per MAC+SSID.

See [probe-request detection](modes/probe-detect.md).

## Baseline anomaly detection

Learn the environment, then alert on changes.

| Command | Parameters | Example |
|---------|------------|---------|
| `BASELINE_START` | `duration[:FOREVER]` (min 60 s) | `@ALL BASELINE_START:300` |
| `BASELINE_STATUS` | None | `@ALL BASELINE_STATUS` |
| `STOP` | None | `@ALL STOP` |

`duration` is the learn phase. After it ends the node enters
monitor mode automatically. See [baseline](modes/baseline.md).

## Active threats

Spot deauth/disassoc floods or drone Remote ID broadcasts.

| Command | Parameters | Example |
|---------|------------|---------|
| `DEAUTH_START` | `secs[:FOREVER]` | `@ALL DEAUTH_START:300` |
| `DRONE_START` | `secs[:FOREVER]` | `@ALL DRONE_START:300` |

See [deauth detection](modes/deauth-detect.md) and
[drone detection](modes/drone-detect.md).

## MAC randomization correlation

Link rotated MACs back to a single device.

| Command | Parameters | Example |
|---------|------------|---------|
| `RANDOMIZATION_START` | `mode:secs[:FOREVER]` | `@ALL RANDOMIZATION_START:2:300` |
| `STOP` | None | `@ALL STOP` |

See [randomization correlation](modes/randomization.md).

## Triangulation

Multi-node RSSI position fix.

| Command | Parameters | Example |
|---------|------------|---------|
| `TRIANGULATE_START` | `target:duration[:rfEnv[:wifiPwr:blePwr]]` | `@HB01 TRIANGULATE_START:AA:BB:CC:DD:EE:FF:60:2:1.5:0.8` |
| `TRIANGULATE_STOP` | None | `@ALL TRIANGULATE_STOP` |
| `TRIANGULATE_RESULTS` | None | `@HB01 TRIANGULATE_RESULTS` |

`rfEnv`: 0 = open sky, 1 = suburban, 2 = indoor, 3 = indoor
dense, 4 = industrial. `wifiPwr` / `blePwr`: 0.1 to 5.0 distance
multiplier. See [triangulation](modes/triangulation.md).

## Security and tamper

| Command | Parameters | Example |
|---------|------------|---------|
| `VIBRATION_ON` / `_OFF` / `_STATUS` | None | `@HB01 VIBRATION_ON` |
| `AUTOERASE_ENABLE` | `setup:erase:vibs:window:cooldown` | `@HB01 AUTOERASE_ENABLE:60:30:3:30:300` |
| `AUTOERASE_DISABLE` / `_STATUS` | None | `@HB01 AUTOERASE_STATUS` |
| `ERASE_REQUEST` | None | `@HB01 ERASE_REQUEST` |
| `ERASE_FORCE` | `<token>` | `@HB02 ERASE_FORCE:HB_12345678_87654321_00001234` |
| `ERASE_CANCEL` | None | `@HB01 ERASE_CANCEL` |

`AUTOERASE_ENABLE` parameters (all in seconds except `vibs`):

- `setup`: grace period after enable before tamper triggers fire.
- `erase`: countdown after tamper detection before the wipe.
- `vibs`: vibration count threshold within the window.
- `window`: rolling window for counting vibrations.
- `cooldown`: minimum time between tamper triggers.

See [security and tamper response](security.md).

## Battery saver

| Command | Parameters | Example |
|---------|------------|---------|
| `BATTERY_SAVER_START` | `interval_minutes` (1-30) | `@HB01 BATTERY_SAVER_START:10` |
| `BATTERY_SAVER_STOP` | None | `@HB01 BATTERY_SAVER_STOP` |
| `BATTERY_SAVER_STATUS` | None | `@HB01 BATTERY_SAVER_STATUS` |

Battery saver stops WiFi/BLE scanning, reduces CPU to 80 MHz,
enables light sleep, and polls GPS once per minute. Mesh UART
stays active. See [battery and power](battery-power.md).

## Heartbeats

Periodic status broadcast over mesh. **Disabled by default.**

| Command | Parameters | Example |
|---------|------------|---------|
| `HB_ON` / `_OFF` | None | `@HB01 HB_ON` |
| `HB_INTERVAL` | `minutes` (1-60) | `@HB01 HB_INTERVAL:10` |

Format: `NODE_ID: Time:YYYY-MM-DD_HH:MM:SS Temp:XX.XC [GPS:lat,lon]`

## Raw BLE forwarding

When enabled, BLE scans emit a second frame per device carrying
the full base64-encoded advertisement payload. Roughly doubles
mesh bytes per detection. Most operators leave it off and let
diginode-cc auto-attach it for the duration of an active scan.

| Command | Parameters | Example |
|---------|------------|---------|
| `RAW_BLE_ON` / `_OFF` / `_STATUS` | None | `@HB01 RAW_BLE_ON` |

Wire frame: `NODE_ID: BLERAW:MAC RSSI CH BASE64_ADV` (typical
length around 80 characters).

## Alert formats

What the node emits when something happens. The first token after
the node ID identifies the alert kind.

| Kind | Format |
|------|--------|
| Target detected | `NODE_ID: Target: TYPE MAC RSSI:dBm [Name:name] [GPS=lat,lon]` |
| Baseline anomaly | `NODE_ID: ANOMALY-NEW/RETURN/RSSI: TYPE MAC RSSI:dBm [details]` |
| Deauth attack | `NODE_ID: ATTACK: DEAUTH SRC:MAC DST:MAC RSSI:dBm CH:N` |
| Triangulation data | `NODE_ID: T_D: MAC RSSI:dBm Type:WiFi/BLE GPS=lat,lon HDOP=X.XX` |
| Triangulation final | `NODE_ID: T_F: MAC=addr GPS=lat,lon CONF=85.5 UNC=12.3` |
| Triangulation complete | `NODE_ID: T_C: MAC=addr Nodes=N [Google Maps link]` |
| Probe watchlist hit | `NODE_ID: PROBE_HIT: MAC RSSI:dBm SSID:"network" [GHOST] [GPS=lat,lon]` |
| Tamper detected | `NODE_ID: TAMPER_DETECTED: Auto-erase in Xs [GPS:lat,lon]` |
| Status response | `NODE_ID: STATUS: Mode:TYPE Scan:STATE Hits:N Temp:XXC Up:HH:MM:SS GPS=lat,lon` |

## See also

- [Operation modes](README.md#operation-modes). Narrative pages
 for each scan-related command.
- Technical handbook: [Mesh protocol](./tech/protocols/mesh.md)
 for the wire-level view.
