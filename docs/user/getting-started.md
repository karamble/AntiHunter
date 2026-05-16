# Getting started

End-to-end first-time setup: from an unflashed XIAO ESP32-S3 to a
Halberd node sitting on your Meshtastic mesh, reporting status.
About twenty minutes if you have the hardware ready.

## What you need

- A XIAO ESP32-S3 in a Halberd carrier (v4 or v5).
- A Heltec WiFi LoRa 32 V3 in the same carrier, paired by UART.
- A USB-C cable from the XIAO to your computer.
- Chrome or Edge on desktop for the easy flash path. Linux command
 line works for everything else.

If you also have a v5 carrier with a XIAO ESP32-C5 fitted, the C5
firmware is flashed separately (`make c5-flash`) and is covered in
the [technical handbook](./tech/firmware/build.md).

## Pick a firmware variant

The XIAO ESP32-S3 runs one of two firmware variants. Both speak the
same mesh-command protocol. Pick at flash time.

- **`halberd-full`**. Includes a local Wi-Fi AP and an in-browser
 control panel at `http://192.168.4.1`. Use this for interactive
 use, bench testing, demos, and the first time you set up a node.
- **`halberd-headless`**. No web UI, smaller binary, mesh-driven
 only. Use this for fleet deployments where diginode-cc or
 another node will drive every operation.

You can switch a node between variants later by re-flashing. Saved
configuration in NVS survives the swap.

## Flash via the web flasher

The easiest path. Works from any Chrome or Edge browser with Web
Serial.

1. Plug the XIAO into your computer over USB-C.
2. Open the [Halberd Web Flasher](./index.html).
3. Select **Full** or **Headless**.
4. Click **Connect & Flash** and pick the XIAO's USB-CDC port from
 the browser prompt.
5. Wait for the flash to finish (about a minute). The page reports
 progress in the embedded terminal.
6. Open the **Configure** panel, fill in node ID, AP credentials,
 scan defaults, RF preset, and click **Send Config**. The flasher
 resets the board, detects the configuration window, and pushes
 the JSON automatically.

The flasher also pushes the host's current Unix time to the RTC on
its way out.

## Flash via the command line

Useful for scripting or headless workstations.

```bash
curl -fsSL -o flashHalberd.sh \
  https://raw.githubusercontent.com/karamble/halberd/main/Dist/flashHalberd.sh
chmod +x flashHalberd.sh
./flashHalberd.sh                 # interactive picker
./flashHalberd.sh -c              # also push a configuration JSON
./flashHalberd.sh -e              # erase first
```

If you have the repo cloned and PlatformIO installed:

```bash
make flash-full                   # default XIAO_PORT=/dev/ttyACM0
make flash-headless
make flash-full XIAO_PORT=/dev/ttyACM2
```

## First boot

On first boot after a fresh flash, the firmware enters a short
"initial configuration mode". The serial console emits a prompt
and waits about a second for either a `CONFIG:` JSON payload or a
`RECONFIG` repeat signal. If neither arrives, the firmware boots
with the last saved NVS configuration (or compiled-in defaults on
a virgin device).

The web flasher's **Send Config** step handles this automatically.
If you're flashing without the flasher, send a JSON payload over
the serial console within the configuration window.

Defaults on a fresh node:

- AP SSID: `Halberd`
- AP password: `antihunt3r123`
- Node ID: auto-generated 2-5 character alphanumeric prefixed
 `HB-` (e.g. `HB-A1B2`). Legacy `AH-` prefixes from older
 AntiHunter firmware are accepted but get migrated to `HB-` on
 first save.
- Scan mode: WiFi + BLE
- RF preset: Balanced

Change the AP credentials before deploying.

## Connect to the AP (full only)

If you flashed `halberd-full`:

1. On your phone or laptop, join the Wi-Fi network named
 `Halberd` with password `antihunt3r123`.
2. Open `http://192.168.4.1` in a browser.
3. The local dashboard appears. Update RF settings, change AP
 credentials, set the target watchlist, and confirm everything
 on the **Diagnostic** page.

`halberd-headless` skips this step entirely. The node accepts mesh
commands and writes to SD without ever bringing up an AP.

## Pair with the Heltec mesh sidekick

The Halberd S3 talks to a Heltec WiFi LoRa 32 V3 over UART1. The
Heltec runs stock Meshtastic with a specific configuration that
pairs it to the Halberd.

If your Heltec is unflashed or running a different config, run:

```bash
make heltec-flash               # flash + configure
make heltec-flash-only          # flash only
make heltec-config-only         # apply config to an already-flashed Heltec
make heltec-confirm-config      # verify expected config
```

Defaults to region `EU_868` and Meshtastic release `2.7.15.567b8ea`.
Override with `HELTEC_REGION=US_915` or `HELTEC_VERSION=...` on the
command line.

## Sanity check on the mesh

From another mesh node (a phone running the Meshtastic app, a
diginode-cc fleet server, or a peer Halberd):

```
@ALL STATUS
```

You should see a reply within a few seconds:

```
HB-A1B2: STATUS: Mode:WiFi+BLE Scan:IDLE Hits:0 Temp:43.2C Up:00:02:14 GPS=48.137,11.576
```

A reply means the link from the Halberd to its Heltec is alive, the
Heltec sees the mesh, and the node is at minimum reachable. If
nothing comes back, check
[troubleshooting](troubleshooting.md#mesh).

## What to do next

- Set a target watchlist with `CONFIG_TARGETS` and run a
 [target hunt](modes/target-hunt.md).
- Run a [device scan](modes/device-scan.md) to see what's in range.
- Run a [baseline](modes/baseline.md) to learn the environment.
- Read the [mesh command reference](commands.md) for everything
 else.

## See also

- [Mesh command reference](commands.md). Every command grouped by
 use case.
- [Web UI tour](web-ui.md). The full-variant dashboard in detail.
- [Troubleshooting](troubleshooting.md). When first boot doesn't
 work.
