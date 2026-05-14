# Web UI tour

`halberd-full` nodes serve a local control panel over Wi-Fi AP at
`http://192.168.4.1`. This page walks through what every screen
does. `halberd-headless` nodes have no web UI. They're mesh-driven
only.

## Joining the AP

Default credentials:

- SSID: `Halberd`
- Password: `antihunt3r123`

Change both before deploying. The configurator lets you update the
SSID + password and the AP channel.

Once joined, open `http://192.168.4.1` in any modern browser. The
AP has no internet uplink. Captive-portal detection in some
operating systems may flag the connection. Ignore it and open the
URL manually if needed.

## Home screen

Lands you on a live-status page:

- Current mode (WiFi / BLE / both).
- Scan state (idle / running / finishing).
- Hit count for the current run.
- Temperature, uptime, GPS fix.
- Quick-start buttons for each detection mode.

## Configurator

Reachable from a button on every page. Settings here mirror what
the web flasher pushes during initial configuration.

- **Identity**: node ID, AP SSID, AP password.
- **Scan defaults**: mode, duration, channels, RF preset.
- **Detection toggles**: heartbeat on/off + interval, vibration
 on/off, auto-erase policy.
- **Targets**: the watchlist used by target hunt and probe
 detection. Pipe-delimited MACs, OUI prefixes, or SSIDs.

Hit **Save** to persist to NVS. Some changes (AP credentials,
channel list) take effect on the next reboot. Others (target
list, RSSI threshold) apply immediately.

## Per-mode pages

One page per detection mode. Each lets you start, stop, and view
the live results.

- **Scanner**. Target hunt UI. Pick mode and duration, click
 Start.
- **Sniffer**. Device scan UI. Has a "Capture Probes" checkbox
 that turns on the probe-request piggyback.
- **Drone**. OpenDroneID drone detection. Shows captured UAV IDs,
 operator locations, and telemetry as they arrive.
- **Baseline**. Learn / monitor anomaly detector. Shows phase,
 device counts, and recent anomalies.
- **Randomization**. MAC randomization correlation results.
 Identity table with linked-MAC lists.
- **Probe Results**. Probe-request intelligence. Per-device
 table with SSIDs, ghost-SSID flags, responding-AP mapping.
- **Deauth Results**. Captured deauth/disassoc frames.

Each page polls the corresponding REST endpoint and refreshes
every few seconds while a scan is running.

## Triangulation orchestration

If the node is configured as the master in a multi-node
triangulation, the **Triangulation** page lets you:

- Start a fix on a specific target MAC.
- Pick the RF environment (open sky, suburban, indoor, indoor
 dense, industrial).
- Adjust Wi-Fi and BLE distance multipliers.
- Watch incoming data frames from peer nodes.
- View the computed lat/lon, confidence, uncertainty, and a
 Google Maps link.

## Data Explorer

Browses the SD card directly. One tab per JSONL log file.

- `/probes.jsonl`. Probe-mode captures.
- `/deauth.jsonl`. Deauth-mode captures.
- `/drones.jsonl`. Drone detections.
- `/vibrations.jsonl`. Tamper events.
- `/halberd.log`. Text system log.
- `/probedb.jsonl`. Persistent probe history database.

See [data formats](data-formats.md) for the schemas.

## Diagnostic page

Shows the node's internal state. Useful for first-boot sanity
checks and troubleshooting.

- GPS fix age and quality (satellites, HDOP).
- SD card mount status and usage percentage.
- Mesh link state (last successful ping with the Heltec).
- Battery telemetry (v5 hardware with UPS module).
- Free heap, uptime, firmware version.

## When to use the web UI vs the mesh

- **Web UI** is for interactive operation. First-time setup,
 ad-hoc scans, browsing results on the spot.
- **Mesh commands** are for automation and fleet operation.
 diginode-cc drives nodes that way. So should your scripts.

You can mix the two. A node configured locally still answers mesh
commands. A node driven over mesh still serves the web UI to
anyone joined to the AP.

## See also

- [Getting started](getting-started.md).
- [Mesh command reference](commands.md) for the parallel remote
 interface.
- [Data formats](data-formats.md) for what's in the SD logs.
- Technical handbook: [REST API reference](./tech/api-rest.md)
 for endpoint-level detail.
