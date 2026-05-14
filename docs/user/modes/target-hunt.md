# Target hunt

You have one or more specific MAC addresses or SSIDs you want to
detect — the classic hunter / "find this device" use case. Target
hunt is the right mode when the question is **"is this thing
nearby?"** and you don't care about everything else.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- A specific known MAC address (target's phone, laptop, IoT device).
- A specific SSID prefix (probe-request hunting).
- Multiple targets in an allowlist (group monitoring).

Not the right mode for:
- "What's around me?" → use [Device scan](device-scan.md).
- "Is the environment normal?" → use [Baseline](baseline.md).

## What this page will cover

- Configuring `CONFIG_TARGETS` — MAC syntax, SSID syntax, allowlist
  vs blocklist semantics.
- `SCAN_START:<mode>:<seconds>[:<channels>][:FOREVER]` syntax with
  every option.
- Mode 0 / 1 / 2 — Wi-Fi only, BLE only, both — when to pick which.
- Channel selection: default vs custom; what to do when you suspect
  the target is on 5 GHz (C5 mirror, v5 only).
- Reading hit notifications on the mesh: hit format, RSSI
  interpretation, dedup window.
- Stopping with `STOP` (or letting `FOREVER` run).

## Worked example

A complete recipe — set targets, start scan, interpret first hits,
stop — will live here.

## See also

- [Mesh command reference](../commands.md) — full syntax tables.
- [MAC randomization correlation](randomization.md) when the target
  is randomizing its MAC.
- [Triangulation](triangulation.md) when you want a position, not
  just a presence flag.
