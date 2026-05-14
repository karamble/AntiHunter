# Device scan

Enumerate every Wi-Fi AP, Wi-Fi client, and BLE advertiser in
range. The right mode when you don't yet know what's out there
and want a snapshot of the environment.

## When to use it

- Site survey before deploying nodes.
- Building an initial picture of an unfamiliar location.
- Feeding the device list into a [baseline](baseline.md).
- Identifying candidate targets for a focused
 [target hunt](target-hunt.md).

Not the right mode for:

- "Is this specific MAC here?" Use target hunt.
- "What changed since yesterday?" Use baseline (in monitor mode).

## Starting a scan

```
@HB01 DEVICE_SCAN_START:<mode>:<seconds>[:FOREVER[:+PROBE]]
```

| Field | Values |
|---|---|
| `mode` | `0` = Wi-Fi only, `1` = BLE only, `2` = both |
| `seconds` | Duration. `FOREVER` overrides to run until `STOP` |
| `+PROBE` | Optional flag. Piggybacks probe-request capture |

Examples:

```
@ALL DEVICE_SCAN_START:2:300                    # 5 minutes, both bands
@HB01 DEVICE_SCAN_START:0:60                    # 1 minute, Wi-Fi only
@ALL DEVICE_SCAN_START:2:300:+PROBE             # also capture probes
@HB01 DEVICE_SCAN_START:2:0:FOREVER:+PROBE      # forever, both, with probes
```

## The `+PROBE` flag

When set, probe-request capture runs alongside the device scan.
Probes feed the same `/probedb.jsonl` database that
[probe-request detection](probe-detect.md) uses. This is the
cheap way to build probe history without dedicating a separate
scan.

Without `+PROBE`, only beacons and BLE adverts populate the
device list. With `+PROBE`, you also get a per-MAC profile of
which SSIDs each Wi-Fi client is probing for.

## Output

Device list grouped by MAC. Each entry includes:

- MAC address.
- Type (`WiFi-AP`, `WiFi-client`, `BLE`).
- SSID (for APs) or device name (for BLE).
- Best RSSI seen during the scan.
- Channel.
- OUI-derived vendor.
- First-seen / last-seen timestamps.
- Randomization flag (LAA bit set).

The web UI presents this as a sortable table on the **Sniffer**
page. Mesh-driven deployments get a compact `STATUS`-style
summary. Pull full details over the REST API
(`/sniffer-cache`).

## Memory limits

The node tracks roughly 200 distinct devices in RAM at a time.
When the cache fills, the oldest entry not on the target list
gets rotated out. For dense environments where you expect more
devices, drop to a shorter scan and rotate through the data more
often, or use a baseline scan which has explicit RAM/SD tiering.

## Worked example

You arrive at a new site and want to know what's there.

```
@HB01 DEVICE_SCAN_START:2:300:+PROBE
```

After 5 minutes the scan completes. Pull the summary:

```
@HB01 STATUS
```

You see counts like "Hits:0 Devices:127 SSIDs:34" in the reply.
For the actual device list, query
`http://192.168.4.1/sniffer-cache` on the AP.

The probe DB now has a record for every probing client. Switch
to probe detection to build deeper per-device profiles:

```
@HB01 PROBE_START:0:1800
```

## See also

- [Probe-request detection](probe-detect.md). Probe-only variant
 with richer per-device fingerprinting.
- [Baseline](baseline.md). Turn this snapshot into ongoing
 anomaly detection.
- [Mesh command reference](./commands.md).
