# Design decisions log

The *why* behind non-obvious choices in the firmware and protocol
design. Useful when revisiting an area later or weighing a future
change against the original intent.

> 🚧 **Stub page.** Content lands in a follow-up commit, lifted
> from the per-session wiki `09-decisions.md`.

## What this page will cover

- **C5 firmware uses ESP-IDF, not Arduino.** 802.15.4, latest BLE 5
  features, and tighter peripheral access drive this.
- **ESP-IDF v6.0.1 pinned**, not v5.5 LTS.
- **COBS + CRC-16 for link framing** (not SLIP, not JSON).
- **Single-byte length field** (max 255 B payload).
- **C5 decodes; doesn't forward raw frames.** Structured records
  cross the link, not raw bytes.
- **GPS push-only at 1 Hz** (not pull-on-demand).
- **Net names from the S3's perspective** (`C5_LINK_TX` is the wire
  on which the S3 transmits).
- **S3 keeps `gpsLat` / `gpsLon` globals as the public API** —
  drop-in replacement, not a new accessor pattern.
- **UART2 on the S3** for the C5 link (was the GPS UART in v4).
- **Boot codec selftest on the C5** but not on the S3 yet.
- **Custom partition table on the C5** (3 MB factory; default
  ESP-IDF caps at 1 MB).
- **C5 `main` component has no `REQUIRES` / `PRIV_REQUIRES`** —
  ESP-IDF's implicit-everything rule applies.
- **5 GHz channel set**: UNII-1 + UNII-2A + UNII-2C; no UNII-3
  (US-only).
- **Country code default "01"** (World Wi-Fi), not "US".
- **DFS channels enabled by default for passive scan** (Halberd
  never transmits).
- **Single-radio scan jobs**, not parallel (C5 2.4 GHz front-end is
  shared between BLE, Wi-Fi, 802.15.4).
- **One commit per stage** on `feat/c5-firmware`.
- **`Halberd/shared/`** as a single source of truth, not duplicated
  headers across S3 and C5 builds.

Each item: the choice, the reasoning, the rejected alternatives,
and what would push the project to revisit it.

## See also

- [Roadmap](roadmap.md) for forward-looking work.
- [Architecture overview](architecture.md).
