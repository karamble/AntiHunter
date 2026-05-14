# Architecture overview

A Halberd node is three coprocessors on one carrier, plus its mesh
peers and an optional fleet control server. This page is the
"30-second tour" — what each chip does, what talks to what, where
the integration surfaces are.

> 🚧 **Stub page.** Content lands in a follow-up commit, distilled
> from wiki pages `01-project-overview.md` and `04-c5-coprocessor.md`.

## What this page will cover

- The carrier inventory: XIAO ESP32-S3 (main MCU, runs Halberd
  firmware), XIAO ESP32-C5 (radio coprocessor — 5 GHz Wi-Fi, BLE 5
  long-range, IEEE 802.15.4, GPS, expansion bus; v5 hardware only),
  Heltec WiFi LoRa 32 V3 (Meshtastic node, off-the-shelf binary).
- The bus map: USB to host, UART0 to flasher / serial console, UART1
  to Heltec, UART2 to C5 (v5) or to GPS (v4), I²C0 to RTC + UPS
  (S3 side), I²C1 to expansion (C5 side).
- The integration surfaces: S3 ↔ C5 link protocol, halberd ↔
  diginode-cc Meshtastic protocol.
- Why the C5 was added (802.15.4, 5 GHz, BLE 5 Coded PHY) and what
  the user-visible benefit is.
- Hardware revisions: v4 (Ø 82 mm, S3-only) shipping today; v5
  (Ø 100 mm, S3 + C5 + UPS module) in development.

## See also

- [Firmware layout](firmware/layout.md) — three firmware trees + the
  shared protocol library.
- [S3 ↔ C5 link protocol](protocols/link.md).
- [Mesh protocol](protocols/mesh.md).
- [Decisions log](decisions.md) for the architectural rationale.
