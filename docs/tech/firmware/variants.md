# `halberd-full` vs `halberd-headless`

The two S3-side firmware variants share their scanning, detection,
and triangulation code — they differ in whether they ship a local
HTTP server / web UI. This page documents what differs and how
to pick one for a deployment.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## What this page will cover

- Per-variant feature matrix:
  - HTTP server (ESPAsyncWebServer + AsyncTCP) — full only.
  - Embedded HTML for the local control panel — full only.
  - REST API endpoints — full only.
  - Mesh-command dispatcher — both.
  - All scan modes, all detection logic — both.
  - SD logging — both.
- Binary size delta (~1.7 MB full vs ~1.3 MB headless after
  stage 8) and RAM delta.
- The PlatformIO env definitions — `halberd-full` vs `halberd-
  headless` in `platformio.ini`, what `build_flags` differ, what
  libraries each pulls.
- When to use which:
  - **Full** — single-node test bench, interactive use, demo, when
    the node sits next to a phone or laptop.
  - **Headless** — fleet deployment, battery-constrained nodes,
    when diginode-cc or a peer node will drive every operation.
- How to switch a deployed node between variants (re-flash; config
  in NVS survives both variants because the field layout is shared).

## See also

- User handbook: [Getting started](../../user/getting-started.md) —
  the user-facing path to pick a variant at flash time.
- [Build + flash](build.md) — the developer-facing PIO commands.
