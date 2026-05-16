# Troubleshooting

Common failures, ordered by where in the lifecycle they show up.

## Flashing

### Web flasher can't see my device

- Make sure you're on Chrome or Edge on desktop. Firefox and
 Safari don't support Web Serial.
- Check the USB cable. Try a different one. Some cables are
 power-only and don't carry data.
- Some Linux distros put serial devices in a restricted group.
 Add your user to `dialout` (Ubuntu/Debian) or `uucp` (Arch),
 log out and back in.
- macOS and Windows usually need no drivers for ESP32-S3 USB-CDC.
 If `/dev/ttyACM0` doesn't show up on Linux, `dmesg | tail`
 after plugging in.

### Flash succeeds but the node never boots

- Most likely a partition-table mismatch. The XIAO ESP32-S3
 ships with the Espressif default partition layout, which is
 fine for Halberd. If you've used the same device for a
 different ESP32 project that wrote a custom partition table,
 erase first with `./flashHalberd.sh -e` or `pio run -e
 halberd-full -t erase -t upload`.
- Try `make c5-monitor` (or `pio device monitor`) right after
 flashing. The boot banner should appear within a few seconds.

### `esptool: A fatal error occurred: Failed to connect`

- Some boards need the BOOT button held during initial USB
 enumeration. Hold BOOT, plug the cable in, release after a
 second.
- Swap USB cables. Power-only cables produce this exact error.
- Try a different USB-C port. Some USB-A to USB-C adapters
 don't carry enough USB 2.0 data lines.

## First boot

### Initial configuration mode timed out

The configuration window is about a second long. If you missed
it:

- The node booted with the previously saved NVS config. If this
 is a brand-new device, that means the compiled-in defaults.
- Re-flash, or reset the device and immediately send the JSON
 payload over the serial console.
- The web flasher's **Send Config** step handles this
 automatically: it resets the board between flash and configure.

### AP shows up but I can't reach `192.168.4.1`

- Some captive-portal detectors on phones interfere. Open the
 URL manually in a browser rather than tapping the
 notification.
- Browser cache from a previous Halberd you've used. Hard-refresh
 or open in an incognito window.
- Confirm you're actually connected to the `Halberd` AP and not
 to your home Wi-Fi.

### Default AP password doesn't work

- The default is `antihunt3r123` (lowercase `antihunt`, no
 capitals, no special characters).
- If a previous flash changed the password, the new value
 persists. Use the password you set, or reset by re-flashing
 with `-e` to erase NVS first.

## Mesh

### No reply to `@ALL STATUS`

- Check the Heltec is actually on. The OLED should show the
 Meshtastic boot screen and channel info.
- Verify the Heltec has the Halberd configuration applied.
 Run `make heltec-confirm-config`.
- Check the UART link between the S3 and the Heltec. The Heltec
 V3 pins are `19 RX / 20 TX`. They connect to the XIAO S3's
 D3 / D4.
- Try sending `@ALL STATUS` from a Meshtastic phone app paired
 with the same Heltec to isolate the issue. If the phone-paired
 Heltec sees nothing, the problem is on the mesh side. If the
 phone sees other nodes but not the Halberd, the problem is the
 Halberd-Heltec UART or the Halberd firmware.

### Heartbeats stop after a few hours

- Most common cause: peer-end battery dying. The peer that was
 receiving and forwarding the heartbeats has lost power.
- Mesh congestion. Too many nodes on the same channel rate-limit
 each other's transmissions. Increase the heartbeat interval
 (`HB_INTERVAL:30`).
- Heltec thermal throttling in a hot enclosure. Check the
 Heltec OLED for the chip temperature.

## Scanning

### Scan starts but no results

- The target may have a hidden SSID. Hidden APs only broadcast
 in response to a probe. Use a probe-detection scan instead, or
 set the SSID in `CONFIG_TARGETS`.
- RSSI threshold is too strict. Default `-80` rejects weak
 signals. Try `CONFIG_RSSI:-90` to widen the window.
- Wrong band. If your target is 5 GHz only, the S3 alone won't
 see it. V5 hardware with a C5 picks up 5 GHz. V4 doesn't.
- Channel list excludes the target's channel. Reset with
 `CONFIG_CHANNELS:1,2,3,4,5,6,7,8,9,10,11,12,13`.

### Lots of `(Hidden)` results

- Passive scanning misses SSID broadcasts because hidden APs
 don't beacon them. Use `SCAN_START` with mode 0 (Wi-Fi
 active) to probe for them.
- This is normal in dense environments. Hidden SSIDs are
 reported with their BSSID and a `(Hidden)` placeholder.

### Drone detection: empty results in known busy airspace

- Many drones broadcast on Bluetooth, not Wi-Fi. V4 hardware
 catches Bluetooth Remote ID legacy (BLE 4). Long-range
 variants (BLE 5 Coded PHY) need v5 hardware with the C5
 coprocessor.
- The drone may not be ASTM F3411 compliant. Older quads
 predate the mandate. Some hobbyist firmware doesn't broadcast
 Remote ID at all.
- Range. Remote ID is low-power and obstacle-blocked. Antenna
 placement matters. Make sure the chassis Wi-Fi antenna is
 vertical and clear of obstructions.

## SD card

### SD card writes slow or drop frames

- Class rating. Use a class 10 or U1 SD card. Anything slower
 bottlenecks during dense scan logging.
- Filesystem. FAT32 is required. ExFAT won't mount.
- Card almost full. The firmware rotates each `.jsonl` at ~1
 MB, but if the SD card itself is near capacity overall, write
 speed suffers.

### SD seems full

- Pull the card and check the rotation files (`_old.jsonl`).
 Each detection mode keeps one rotation pair. Older history is
 overwritten.
- `/halberd.log` doesn't rotate. Truncate it if it grows beyond
 what you want to keep.

## Battery

### Draining fast

- Check the RF preset. Aggressive halves runtime compared to
 Balanced.
- Active BLE scanning (mode 1 or 2) draws more than passive Wi-Fi
 alone. Drop to mode 0 if you only need Wi-Fi coverage.
- Heartbeat duty. Frequent heartbeats wake the whole mesh.
 Increase `HB_INTERVAL` to 30 minutes if you're not actively
 watching the node.
- Confirm battery saver isn't being toggled on and off rapidly.
 `BATTERY_SAVER_STATUS` shows the current state.

## How to file a bug

If your symptom isn't here, open an issue on GitHub. Include:

- The boot banner output (first 50 lines after power-on).
- The exact command you sent and the exact reply (or the absence
 of one).
- A screenshot of the diagnostic page if you have access to the
 web UI.
- Hardware revision (v4 / v5).
- Firmware variant (`halberd-full` / `halberd-headless`) and
 approximate build / commit if known.

## See also

- [Getting started](getting-started.md). If first boot doesn't
 work, start there.
- [Battery + power tuning](battery-power.md) for power-related
 issues.
- Technical handbook: [Build + flash](./tech/firmware/build.md)
 for rebuild-from-source recovery paths.
