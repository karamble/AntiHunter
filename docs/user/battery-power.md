# Battery and power tuning

Halberd is designed for hours-to-days of battery operation depending
on scan intensity. This page covers the knobs that move that number:
RF presets, battery saver, scan duty cycle, and the UPS module power
budget on v5 hardware.

> 🚧 **Stub page.** Content lands in a follow-up commit. The
> [README's RF Configuration section](../../README.md#rf-configuration)
> covers the preset tuning in the meantime.

## What this page will cover

- The four RF presets — **Relaxed**, **Balanced**, **Aggressive**,
  **Custom** — what each one trades off (channel dwell time, scan
  cadence, BLE interval/window, expected current draw).
- Battery saver: how `BATTERY_SAVER_START:<minutes>` cycles the radio
  duty, expected runtime extension, what stays on (mesh, GPS, RTC)
  vs. what gets gated.
- Heartbeat interval — `HB_INTERVAL:<minutes>` impact on mesh
  airtime + your peers' battery (heartbeats wake their listeners).
- Power-source-aware behaviour on v5 (when a Waveshare UPS Module 3S
  is attached): INA219 telemetry, low-battery thresholds, when to
  auto-engage battery saver.
- Practical runtime estimates: 2S 18650 (v4) vs 3S 18650 (v5), each
  RF preset, idle vs sustained scan.

## See also

- [Battery commands](commands.md) — the `BATTERY_SAVER_*` and
  `HB_*` command syntax.
- [Troubleshooting](troubleshooting.md) — "my node is draining fast"
  diagnostic flow.
