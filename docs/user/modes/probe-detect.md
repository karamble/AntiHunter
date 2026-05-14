# Probe-request detection

Capture 802.11 probe requests and probe responses, build a
per-device profile from the SSIDs each client probes for, and
surface "ghost" SSIDs (probed for but never seen replying
nearby). On v5 hardware, the C5 coprocessor extends this to
5 GHz channels.

## What probes reveal

When a Wi-Fi client wakes up, it sends probe requests asking "is
this SSID nearby?" for every network in its preferred-network
list. That list is a fingerprint. A phone probing for
`HomeWifi`, `WorkSecure`, and `Starbucks` tells you where its
owner has been even if you've never seen them before.

A "ghost SSID" is one a client probes for but no AP nearby
responds to. Ghost SSIDs are usually networks the client
connected to elsewhere. Travel patterns. Home networks. Work
networks. They show up in result tables prefixed with `~` (e.g.
`~"HomeOffice"`).

## When to use it

- Identifying clients by their preferred-network list rather
 than just their MAC.
- Tracking people or devices that visit multiple locations (the
 SSID list survives across visits even when the MAC rotates).
- Detecting MAC randomization (probes with random MACs but
 consistent SSID sets often belong to the same device).
- Adding probe history to a database for retrospective
 correlation.

## Starting a scan

```
@HB01 PROBE_START:<mode>:<seconds>[:FOREVER][:+ALL]
```

| Field | Values |
|---|---|
| `mode` | `0` = Wi-Fi (recommended), `1` = BLE, `2` = both |
| `seconds` | Duration. `FOREVER` overrides to run until `STOP` |
| `+ALL` | Broadcast every probe to the mesh, not just target matches |

Stop with `@HB01 PROBE_STOP` or wait.

### `+ALL` flag

Without `+ALL`, only probes that match `CONFIG_TARGETS` get
broadcast to the mesh. The full capture still lands in
`/probes.jsonl` on SD. Use `+ALL` when you want every probe
echoed to diginode-cc for live analysis (subject to the 60 s
per-MAC+SSID dedup).

### Mode considerations

- **Mode 0 (Wi-Fi)** is the primary use case. Probes are a
 Wi-Fi concept.
- **Mode 1 (BLE)** runs a thin BLE scan in parallel and adds
 BLE-side MAC target matches. There's no BLE equivalent of
 probes.
- **Mode 2 (both)** is mode 0 + mode 1.

Stick to mode 0 unless you specifically want concurrent BLE
target watching.

## Output

Per-device records in `/probes.jsonl` and the in-RAM result
table. Each device shows:

- MAC, OUI vendor.
- RSSI min / current / max.
- Last channel observed.
- Probe count.
- Up to 4 SSIDs the device probed for.
- Ghost-SSID flags (`~` prefix on entries with no responding
 AP nearby).
- Responding-AP BSSID + SSID when a probe response was captured.
- History annotations from `/probedb.jsonl` (total times seen
 across all sessions, first-seen, last-seen).

See [data formats](./data-formats.md) for the full JSONL
schema.

## On v5 hardware

When the C5 coprocessor is present, the S3's probe-mode task
re-arms it every 10 seconds with a 9-second 5 GHz sniff window.
5 GHz probes feed the same `probeDevices` map and dedup on MAC.
A device probing on both bands shows up as one entry with
channels from both bands aggregated.

This is the stage 8 work (commit `00443a1`). Nothing changes
about the user-facing command syntax. The 5 GHz channels just
quietly start appearing in your results.

## Worked example

You're at a coffee shop and want to profile the customers'
preferred-network lists.

```
@HB01 PROBE_START:0:1800:+ALL
```

30 minutes, Wi-Fi only, broadcast everything. Live results flow
to the mesh. After the scan ends, pull the full database:

- Web UI: **Probe Results** tab.
- REST: `GET /api/probedb`.
- SD: pull the card and read `/probes.jsonl` directly.

You'll see entries like:

```json
{"t":1747237891,"mac":"AA:BB:CC:DD:EE:FF","rssi":-58,"ch":6,"cnt":12,"rand":false,"v":"Apple","ss":["~HomeOffice","CafeFree","~UniSecure"],"hit":false}
```

Two ghost SSIDs reveal home and university networks. One
non-ghost SSID is the coffee shop itself. A second visit by the
same MAC adds to the session count in `probedb`.

## See also

- [Target hunt](target-hunt.md). When you already know the MAC
 or SSID.
- [MAC randomization correlation](randomization.md). For
 fingerprinting beyond what probes alone provide.
- [Data formats](./data-formats.md). `/probes.jsonl` and
 `/probedb.jsonl` schemas.
