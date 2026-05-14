# Roadmap

What's planned next on Halberd, what's a candidate but not committed,
and what's intentionally out of scope. This page tracks forward-
looking work; the [decisions log](decisions.md) tracks what's been
chosen and why.

> 🚧 **Stub page.** Content lands in a follow-up commit, sourced
> from the per-session wiki `07-roadmap.md`.

## What this page will cover

### Completed major branches

- **`feat/c5-firmware`** — C5 coprocessor bring-up through expansion
  bus (stages 1–7 shipped; stage 8 post-roadmap added a 5 GHz
  probe-request sniffer wired into S3 probe mode).

### Candidate follow-ups (no committed timeline)

- **C5-side log forwarding** (`LINK_MSG_LOG`) — multiplex the C5's
  `ESP_LOG*` over the link so one USB cable monitors both
  coprocessors.
- **Firmware update over the link** — S3 streams new C5 firmware,
  C5 writes the OTA partition. Needs a partition-table change
  (currently single-factory).
- **GPS pull model** (`LINK_MSG_GPS_QUERY`) — on-demand fix request
  alongside the existing 1 Hz push.
- **PPS wire-mod** — bring the GPS PPS pin to the C5 for hardware-
  precise 1 Hz tick.
- **Country code runtime config** — currently build-time.
- **I²C bus auto-scan on boot** — surface "I see devices at 0x40,
  0x68" as a startup beacon.
- **BLE / 802.15.4 wiring into probe mode** — stage 8 sibling that
  wasn't asked for in the original probe-sniffer ask.

### Hardware-side roadmap

- v5 carrier fab + assembly + bench validation of stages 4–8.
- Documentation of v5 assembly (deferred from this docs split per
  user direction).

## See also

- [Decisions log](decisions.md) — the "why" anchor for the choices
  baked into what's shipped already.
