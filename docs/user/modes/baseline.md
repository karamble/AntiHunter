# Baseline anomaly detection

Learn what "normal" looks like in a location over a period of
time, then alert when something deviates. New devices arriving.
Known devices vanishing. RSSI shifts suggesting movement.

## When to use it

- Unattended monitoring of a fixed location.
- Perimeter alerting where anything unusual is interesting.
- Pre-event reconnaissance followed by during-event monitoring.
- Detecting deauth/disassoc floods indirectly (large numbers of
 clients suddenly leaving an AP show up as anomalies).

Not the right mode for:

- "Find this specific MAC right now". Use
 [target hunt](target-hunt.md).
- "What's in range right now?". Use
 [device scan](device-scan.md).

## How it works

Two phases.

1. **Learn phase**. For the configured duration, the node
 catalogs every device it sees. Each MAC enters a baseline
 database with first-seen / last-seen / RSSI history.
2. **Monitor phase**. After learning, the node continues
 scanning indefinitely. Anything that doesn't match the
 baseline is an anomaly. Three anomaly types:
 - **NEW**: MAC not seen during learn phase.
 - **RETURN**: MAC seen during learn phase, then absent for
 a while, now back.
 - **RSSI**: known MAC, but its RSSI has shifted significantly
 (suggesting the device moved).

Anomalies are alerted on the mesh and logged to SD.

## Starting a scan

```
@HB01 BASELINE_START:<duration>[:FOREVER]
```

| Field | Values |
|---|---|
| `duration` | Learn-phase length in seconds (minimum 60) |
| `FOREVER` | Optional. Stay in learn phase forever (no monitor) |

Examples:

```
@HB01 BASELINE_START:300                # 5-min learn, then monitor
@HB01 BASELINE_START:3600               # 1-hour learn, then monitor
@HB01 BASELINE_START:60:FOREVER         # learn forever, no anomalies yet
```

Check phase:

```
@HB01 BASELINE_STATUS
```

Stop entirely with `@HB01 STOP`. The baseline DB persists to
SD and survives reboots, so you can resume monitoring across
power cycles by starting a fresh `BASELINE_START` with the same
parameters. Manual reset via the web UI or
`POST /baseline/reset`.

## How long to learn for

Rule of thumb: 3 to 10 times the longest normal-presence cycle
in the environment.

- An office: most people stay for 8 hours, so a 1-day learn
 phase catches the daily rhythm.
- A weekend retreat: a week to capture weekly visitors.
- A continuously occupied location: 1 hour is usually enough.

Too short and you'll get many false NEW alerts as the
not-yet-learned-about regulars trickle in. Too long is harmless
except that you start alerting later.

## Output

When an anomaly fires, the node broadcasts:

```
HB-A1B2: ANOMALY-NEW: WiFi AA:BB:CC:DD:EE:FF RSSI:-58 SSID:"strange"
HB-A1B2: ANOMALY-RETURN: BLE 11:22:33:44:55:66 RSSI:-72 absent 4h13m
HB-A1B2: ANOMALY-RSSI: WiFi 22:33:44:55:66:77 RSSI:-42 was:-78
```

Anomalies also land on the web UI **Baseline Results** page
with timestamps and context.

## Tiering

The baseline stores:

- **RAM cache**: 200 to 500 devices, hot path.
- **SD overflow**: 1K to 100K devices (1500 default without an
 explicit configuration).

Devices fall out of the RAM cache after a long absence. Their
records stay on SD. If they return, they get promoted back to
RAM and trigger a RETURN anomaly.

## Worked example

You want unattended monitoring of a workshop. People come and
go during the day. Nights and weekends should be quiet.

```
@HB01 BASELINE_START:86400              # 24-hour learn phase
```

After 24 hours the node has every regular's MAC catalogued. It
auto-enters monitor mode. Subsequent alerts on the mesh:

```
HB-A1B2: ANOMALY-NEW: WiFi DE:AD:BE:EF:00:01 RSSI:-65
```

A device the workshop has never seen during business hours.
Diginode-cc records the alert, correlates with the GPS in the
heartbeat, and notifies you.

## See also

- [Device scan](device-scan.md). The underlying enumeration the
 baseline learns from.
- [Mesh command reference](./commands.md).
- Web UI: the **Baseline** page shows phase, counts, and
 reset/configure controls.
