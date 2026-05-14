# Battery and power tuning

Halberd is designed for hours-to-days of battery operation
depending on scan intensity. This page covers the knobs that move
that number: RF presets, the battery saver, scan duty cycle, and
(on v5 hardware) the Waveshare UPS Module 3S power budget.

## RF presets

The biggest single lever. Four presets, configurable from the
configurator or the `/rf-config` API.

| Preset | Channel dwell | Scan cadence | BLE interval / window | Current draw |
|---|---|---|---|---|
| **Relaxed** | longer | sparse | wider window | lowest |
| **Balanced** (default) | medium | medium | medium | medium |
| **Aggressive** | short | dense | narrow window | highest |
| **Custom** | user-set | user-set | user-set | depends |

Trade-off: a shorter dwell means the radio sweeps channels faster
and catches more devices, but each channel gets fewer milliseconds
to see weak signals. Aggressive presets miss less but burn battery
significantly faster.

For unattended fleet nodes, Relaxed or Balanced are the right
defaults. For active hunts where you can re-charge, Aggressive is
fine.

## Battery saver mode

```
@HB01 BATTERY_SAVER_START:10
```

Engages a duty-cycled mode that trades coverage for runtime.
Parameter is `interval_minutes` (1 to 30). The node:

- **Stops** WiFi and BLE scanning entirely.
- **Reduces** CPU clock from 240 MHz to 80 MHz.
- **Enables** light sleep between mesh polls.
- **Polls** GPS once per minute instead of continuously.
- **Keeps** the mesh UART active so it still answers mesh commands.
- **Emits** heartbeats marked `Battery:SAVER` instead of normal
 scan-state heartbeats.

Disengage with `BATTERY_SAVER_STOP`. Check state with
`BATTERY_SAVER_STATUS`.

Battery saver is the right tool when you want a node to stay
reachable but quiescent. It is not the right tool when you want
*reduced* but still active scanning. For that, drop to the
Relaxed RF preset and increase your `HB_INTERVAL`.

## Heartbeat duty

Heartbeats wake the whole mesh. Every node listening on the
channel processes each incoming heartbeat. Frequent heartbeats
from one node degrade everyone else's battery.

Defaults: heartbeats **off**. Turn on with `HB_ON` and set the
interval with `HB_INTERVAL:<minutes>` (1 to 60). For a fleet of
nodes, 10 to 30 minutes is a reasonable range. Tighter
intervals are useful during active operations.

## Runtime estimates

Rough order-of-magnitude figures. Actual runtime depends on
battery capacity, ambient temperature, scan target density, and
how often the radio finds something worth logging.

v4 hardware (2S 18650, ~6 Wh usable):

| Mode | Approx runtime |
|---|---|
| Idle (Balanced preset, no scans running) | 12 to 24 hours |
| Continuous scan (Balanced) | 6 to 10 hours |
| Continuous scan (Aggressive) | 4 to 6 hours |
| Battery saver (10 min heartbeat) | 60 to 100 hours |

v5 hardware (3S 18650 via Waveshare UPS, ~9 Wh usable):

| Mode | Approx runtime |
|---|---|
| Idle (Balanced) | 18 to 36 hours |
| Continuous scan (Balanced) | 8 to 14 hours |
| Continuous scan (Aggressive, both radios) | 5 to 8 hours |
| Battery saver (10 min heartbeat) | 100 to 160 hours |

The C5 coprocessor adds maybe 80 mA when its radio is active, but
only during a scan job (single-radio scheduling). Idle C5 draw is
modest.

## UPS telemetry (v5 only)

The Waveshare UPS Module 3S exposes an INA219 chip on the S3's
I²C bus. The S3 reads battery voltage, current, and pack state of
charge. Visible on the diagnostic page in the web UI and on the
`/diag` endpoint.

When the node detects low battery, it does not automatically
engage battery saver. That's a deliberate choice. Battery saver
disables most of what the node does. If your deployment wants
auto-engage, drive it from diginode-cc against the INA219
telemetry.

## Low-current tips

If you're trying to maximise runtime:

- Set the RF preset to **Relaxed**.
- Restrict the Wi-Fi channel list to just 1 / 6 / 11 (skip 36 to
 165 unless you really need 5 GHz).
- Use `BATTERY_SAVER_START:30` between scans rather than running
 continuously.
- Disable heartbeats (`HB_OFF`) if you have alternative liveness
 signals.
- Turn off vibration sensing if the node sits in a fixed mount
 that won't move (`VIBRATION_OFF`).

## See also

- [Mesh command reference](commands.md). All the
 `BATTERY_SAVER_*` and `HB_*` syntax.
- [Troubleshooting](troubleshooting.md). "Draining fast" diagnostic
 flow.
