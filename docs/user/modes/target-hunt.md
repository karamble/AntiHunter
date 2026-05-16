# Target hunt

You have one or more specific MAC addresses or SSIDs you want to
detect. The classic hunter use case: "is this thing nearby?".
Target hunt is the right mode when you know what you're looking
for and you don't care about the rest of the environment.

## When to use it

- A specific known MAC address (target's phone, laptop, IoT
 device).
- An OUI prefix (the first three octets of a MAC, identifying
 the vendor).
- A specific SSID (probe-hunting by network name).
- A pipe-delimited mix of all three.

Not the right mode for:

- "What's around me?" Use [device scan](device-scan.md).
- "Is the environment normal?" Use [baseline](baseline.md).
- "Where exactly is this MAC?" Use [triangulation](triangulation.md)
 after target hunt confirms presence.

## Setting targets

The watchlist is the same one used by probe detection.

```
@HB01 CONFIG_TARGETS:AA:BB:CC:DD:EE:FF|11:22:33|MyNetwork
```

Pipe-delimited list. Each entry is one of:

- A full MAC (`AA:BB:CC:DD:EE:FF`).
- An OUI prefix (`11:22:33`, three octets only).
- A SSID (`MyNetwork`, case-sensitive).

Targets persist to NVS and survive reboots. Use the web UI
configurator on `halberd-full` for an editable list.

## Starting a scan

```
@HB01 SCAN_START:<mode>:<seconds>[:<channels>][:FOREVER]
```

| Field | Values |
|---|---|
| `mode` | `0` = Wi-Fi only, `1` = BLE only, `2` = both |
| `seconds` | Duration. `FOREVER` overrides to run until `STOP` |
| `channels` | Comma-separated Wi-Fi channels. Default uses the saved list |

Examples:

```
@ALL SCAN_START:2:300                   # 5 minutes, Wi-Fi + BLE, default channels
@ALL SCAN_START:0:60:1,6,11             # 1 minute, Wi-Fi only, channels 1/6/11
@HB01 SCAN_START:1:300:FOREVER          # BLE only, forever
```

Stop with `@HB01 STOP` (or wait for `seconds` to elapse).

## Reading hits

When a target is detected, the node broadcasts:

```
HB-A1B2: Target: BLE AA:BB:CC:DD:EE:FF RSSI:-58 [Name:iPhone] [GPS=48.137,11.576]
```

Fields:

- `TYPE` is `WiFi` or `BLE`.
- `RSSI` is the signal strength in dBm. Closer to 0 means closer
 to the node. Below -85 means far or obstructed.
- `Name` appears when the device advertised one (BLE devices
 often do. Wi-Fi clients rarely).
- `GPS=` appears when the node has a current fix.

Hits dedup at 3 seconds per target. A target that stays in range
generates one hit, then nothing for 3 seconds, then another, and
so on.

## On v5 hardware

The C5 mirrors 2.4 GHz scans onto 5 GHz channels automatically.
Hits from 5 GHz APs are tagged `(C5/5GHz)` in the log line.
Nothing changes about the command syntax. If your target is
exclusively 5 GHz, v4 hardware will miss it. V5 will catch it.

## Worked example

You want to know when phone MAC `AA:BB:CC:DD:EE:FF` shows up at
your test site.

```
@HB01 CONFIG_TARGETS:AA:BB:CC:DD:EE:FF
@HB01 SCAN_START:2:300:FOREVER
```

The node now watches both bands forever. When the phone
arrives:

```
HB-A1B2: Target: WiFi AA:BB:CC:DD:EE:FF RSSI:-72 [GPS=48.137,11.576]
HB-A1B2: Target: BLE AA:BB:CC:DD:EE:FF RSSI:-65 [Name:Apple]
```

Wi-Fi and BLE see the same phone with different MACs sometimes
(MAC randomization). The probe-detect mode + randomization mode
help link those. To know exactly where the phone is, follow up
with `TRIANGULATE_START`.

Stop the watch when you're done:

```
@HB01 STOP
```

## See also

- [Mesh command reference](./commands.md). Full syntax.
- [MAC randomization correlation](randomization.md). When the
 target rotates its MAC.
- [Triangulation](triangulation.md). When you want a position,
 not just a presence flag.
