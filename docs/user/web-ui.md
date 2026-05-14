# Web UI tour

`halberd-full` nodes serve a local control panel over Wi-Fi AP at
`http://192.168.4.1`. This page walks through every screen. What
each control does, what each result table means, and how to drive a
scan from start to finish without touching the mesh.

> 🚧 **Stub page.** Content lands in a follow-up commit. The
> [README's Features section](././README.md#features) describes the
> backing functionality in the meantime.

## What this page will cover

- Joining the node's AP (`Halberd` / `antihunt3r123` defaults. Change
 these in the configurator).
- The home screen: live status, current scan state, quick-start
 buttons.
- The configurator: WiFi/AP credentials, node ID, scan defaults, RF
 preset, heartbeat, vibration, auto-erase.
- Per-mode pages: scanner, sniffer, drone, baseline, randomization
 results, probe results, deauth results.
- The data explorer: browsing `probes.jsonl`, `deauth.jsonl`,
 `drones.jsonl`, `vibrations.jsonl`, and `halberd.log` straight from
 the SD card.
- The diagnostic page: GPS fix, SD card status, mesh link health,
 battery (if UPS connected).
- Triangulation orchestration when a node is configured as the
 master in a multi-node fix.

## When to use the web UI vs. Mesh commands

- The web UI is the right interface for **interactive use**. First-
 time setup, ad-hoc scans, browsing results on the spot.
- The mesh-command interface is the right interface for **automation
 and fleet operation**. Diginode-cc drives it, you should too if
 you're scripting.
- `halberd-headless` has no web UI at all. Everything is mesh-driven.

## See also

- [Getting started](getting-started.md)
- [Data formats](data-formats.md) for what's in the JSONL files
- Technical handbook: [REST API reference](./tech/api-rest.md) for
 endpoint-level detail and JSON schemas
