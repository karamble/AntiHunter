# Getting started

End-to-end first-time setup: from an unflashed XIAO ESP32-S3 to a
Halberd node sitting on your Meshtastic mesh, reporting status. About
20 minutes if you have the hardware ready.

> 🚧 **Stub page.** Content lands in a follow-up commit. See the
> [README's Quick Start](../../README.md#quick-start) in the meantime.

## What this page will cover

- Choosing between `halberd-full` and `halberd-headless` firmware.
- Flashing via the [web flasher](../index.html) (Chrome / Edge, Web
  Serial) — the easy path.
- Flashing via `esptool.py` from the command line — the headless /
  scriptable path.
- The first-boot "INITIAL CONFIGURATION MODE" window: AP SSID,
  password, node ID, scan defaults, RF preset.
- Joining the device's Wi-Fi AP (default SSID `Halberd`, password
  `antihunt3r123`) and reaching `http://192.168.4.1` for the local
  configurator (`halberd-full` only).
- Pairing with a Heltec V3 running stock Meshtastic — wiring, region,
  baud, the `scripts/flash-heltec-meshtastic.sh` helper.
- A first sanity-check command (`STATUS`) from another mesh node and
  what a healthy reply looks like.

## See also

- [Mesh command reference](commands.md)
- [Web UI tour](web-ui.md) (`halberd-full` only)
- [Troubleshooting](troubleshooting.md) if first boot doesn't work
