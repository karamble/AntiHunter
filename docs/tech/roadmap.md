# Roadmap

What's shipped, what's planned, and what's intentionally out of
scope. This page tracks forward-looking work; the
[decisions log](decisions.md) tracks what's been chosen and why.

## Shipped — `feat/c5-firmware`

The C5 coprocessor branch went from empty repo to a fully-featured
multi-radio coprocessor across eight stages. Each is one commit and
can be reverted individually.

| # | Title | Commit |
|---:|---|---|
| 1 | Project scaffold (ESP-IDF v6.0.1 bring-up)       | `1684e25` |
| 2 | UART link layer (COBS+CRC framing, PING/PONG)    | `5b85fe2` |
| 3 | GPS handover from S3 to C5 via link              | `0a1e915` |
| 4 | 5 GHz Wi-Fi scan mirror                          | `0e14416` |
| 5 | BLE scan mirror (1M + Coded PHY, ext-adv)        | `022eb21` |
| 6 | IEEE 802.15.4 frame sniffer + protocol decode    | `7c8e1d8` |
| 7 | Expansion bus (I²C + GPIO over link)             | `e3189f2` |
| 8 | 5 GHz probe-request sniffer (post-roadmap)       | `00443a1` |

Stage 8 was a follow-up after the original 7-stage roadmap completed;
the user noticed probe mode's 2.4 GHz-only blind spot and we
extended it.

## Candidate follow-ups

None of these are committed timelines — they're listed for
discoverability when revisiting the project.

### Firmware

- **C5-side log forwarding** (`LINK_MSG_LOG`). Multiplex the C5's
  `ESP_LOG*` output over the link to the S3's USB serial so one
  cable monitors both coprocessors. Today you need `make c5-monitor`
  on a separate USB cable to see C5 logs.
- **Firmware update over the link**. S3 streams new C5 firmware
  bytes over the link, C5 writes the OTA partition. Requires a
  partition-table change (currently single-factory at 3 MB; OTA
  would need 1+1+1 MB or smaller binaries).
- **GPS pull model** (`LINK_MSG_GPS_QUERY`). On-demand fix request
  in addition to the 1 Hz push. Adds a knob, breaks nothing.
- **Country code runtime config**. Currently build-time. Move to an
  NVS-stored value the S3 updates via a link message.
- **BLE / 802.15.4 wiring into probe mode**. Stage 8 only wired
  the 5 GHz Wi-Fi sniffer into probe mode. BLE in probe mode is
  still a thin MAC-target matcher; 802.15.4 sweeps every 10 s as
  side traffic. Both could become richer in the same way the
  Wi-Fi path is now.

### Hardware

- **PPS wire-mod**. The GPS PPS pin is exposed on the connector
  but not wired to the C5 on v5. A hand-mod fly-wire would give the
  C5 a hardware-precise 1 Hz tick, useful for RTC discipline and
  tighter triangulation timing.
- **I²C bus auto-scan on boot**. Have the C5 emit a "I see devices
  at 0x40, 0x68" notification at init time so the S3 knows what's
  on the expansion bus without polling.
- **v5 carrier fab + assembly + bench validation**. All eight stages
  compile clean across `halberd-c5`, `halberd-headless`,
  `halberd-full`. Each stage documented per-stage hardware checks
  in `06-progress.md` style — bring those to the bench when v5 is
  in hand.

### Documentation

- **Public hardware build guide**. The internal `hw/pcb/docs/`
  notes (BOM, schematic, KiCad walkthroughs) are `.gitignore`d
  working drafts. Polishing and publishing them under
  `docs/tech/hardware/` is a candidate later pass.
- **diginode-cc integration handbook**. The mesh-protocol reference
  on this site is one side of the story; a parallel reference in
  the `karamble/diginode-cc` repo covers the server's parse and
  storage paths.

## Out of scope

What's intentionally **not** on any roadmap.

- **BLE GATT exploration**. We're scan-only. Connecting to
  advertised services would change the threat model (transmit,
  detectable) and is out of mission.
- **Active Wi-Fi attacks**. Halberd never transmits frames. No
  deauth, no probe injection, no rogue AP. The firmware deliberately
  doesn't include `esp_wifi_80211_tx`.
- **Active 802.15.4 / Zigbee join attempts**. Promiscuous capture
  only.
- **Raw-frame forwarding to diginode-cc**. The C5 decodes; the
  fleet server receives structured records. This is a deliberate
  user directive — see [decisions](decisions.md) "C5 decodes;
  doesn't forward raw frames".
- **Streaming sensor reads on the expansion bus** (1 kHz IMU etc.).
  Stage 7 is synchronous request/response only. Streaming would
  need a new message type and a different commit; defer until a use
  case appears.

## See also

- [Decisions log](decisions.md) — the "why" anchor for everything
  shipped above.
- [C5 coprocessor](firmware/c5-coprocessor.md) — per-stage detail.
