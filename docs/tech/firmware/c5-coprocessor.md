# C5 coprocessor

The Seeed XIAO ESP32-C5 is the second MCU on a v5 Halberd carrier.
It owns 5 GHz Wi-Fi, BLE 5 (1M + 2M + Coded PHY), IEEE 802.15.4
(Zigbee / Thread / Matter), the GPS UART, and the I²C / GPIO
expansion bus. The S3 talks to it over a 921600-baud UART link with
COBS-framed messages.

> 🚧 **Stub page.** Content lands in a follow-up commit, sourced
> from the per-session wiki `04-c5-coprocessor.md` plus the
> per-stage progress notes.

## What this page will cover

- Responsibilities, by radio:
  - **5 GHz Wi-Fi**: scan mirror (stage 4) + 5 GHz probe-request
    sniffer wired into S3 probe mode (stage 8).
  - **BLE**: NimBLE host stack, 1M + Coded PHY scan, extended adv
    (stage 5).
  - **IEEE 802.15.4**: promiscuous-mode RX, Zigbee / Thread /
    Matter classifier (stage 6).
  - **GPS**: NMEA parser, 1 Hz `LINK_MSG_GPS_FIX` push to the S3
    (stage 3).
  - **Expansion bus**: synchronous I²C read/write + EXP_GPIO
    config/write/read (stage 7).
- Stage history (commits on `feat/c5-firmware`): scaffold → link
  layer → GPS → 5 GHz scan → BLE → 802.15.4 → expansion bus →
  5 GHz probe sniffer.
- Why ESP-IDF instead of Arduino: 802.15.4 driver availability,
  latest BLE 5 features, direct radio peripheral access.
- Partition layout: 3 MB factory (default 1 MB caps too small for
  the multi-radio stack); 4 MB flash size set in
  `sdkconfig.defaults`.
- Boot codec selftest — what the `selftest_cb` does and why it runs
  before the UART driver comes up.
- Radio scheduling: single-job-at-a-time across BLE / Wi-Fi 2.4 /
  802.15.4 (shared 2.4 GHz front-end); the protocol's `BUSY`
  status as the back-pressure mechanism.

## See also

- [S3 ↔ C5 link protocol](../protocols/link.md) for the wire
  format.
- [Layout](layout.md) for the C5's place in the tree.
- [Decisions log](../decisions.md) for the architectural rationale.
