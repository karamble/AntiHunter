# Troubleshooting

Common failures, ordered by where in the lifecycle they show up:
flashing, first boot, mesh pairing, scanning, results.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## What this page will cover

### Flashing

- "Web flasher can't see my device" — driver / browser / cable triage.
- "Flash succeeds but the node never boots" — partition-table
  mismatch, factory-binary corruption.
- "esptool: A fatal error occurred: Failed to connect" — boot mode
  hold, USB-C cable swap.

### First boot

- "INITIAL CONFIGURATION MODE timed out" — what to do when the
  configurator window closes before you've sent the JSON.
- "AP shows up but I can't reach `192.168.4.1`" — captive-portal
  interference, DNS, browser cache.
- "Default password doesn't work" — what `antihunt3r123` defaults to
  on a freshly-provisioned node vs. a node you've previously
  configured.

### Mesh

- "No `STATUS` reply from my node" — Heltec config drift, baud
  mismatch, missing regional setting.
- "Heartbeats stop after a few hours" — peer-end battery, mesh
  congestion.

### Scanning

- "Scan starts but no results" — hidden SSID, RSSI threshold too
  strict, wrong band.
- "Lots of `(Hidden)` results" — passive scan vs. active probe trade-
  offs.
- "Drone detection: empty results in known busy airspace" — Wi-Fi
  Remote ID vs. Bluetooth Remote ID, OpenDroneID frame type
  filtering.

### SD card

- "SD card writes slow / drop frames" — class rating, file-system,
  rotation pressure.
- "SD seems full" — log rotation behaviour.

### Battery

- "Draining fast" — RF preset, BLE active vs. passive, mesh
  heartbeat duty cycle.

## How to file a bug

If your symptom isn't here, open an issue on GitHub with the boot
banner output, the command you sent, and the reply (or lack of
reply). Diagnostic-page screenshots help.

## See also

- [Battery + power tuning](battery-power.md) for power-related issues.
- Technical handbook: [Build + flash](../tech/firmware/build.md) for
  rebuild-from-source recovery paths.
