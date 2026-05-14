# Architecture overview

A Halberd node is three coprocessors on one carrier, plus its mesh
peers and an optional fleet control server. This page is the
30-second tour: what each chip does, what talks to what, where the
integration surfaces are.

## The carrier

Two hardware revisions are in play.

- **v4** (Ø 82 mm, shipping today). XIAO ESP32-S3 main MCU, Heltec
 WiFi LoRa 32 V3 mesh sidekick, ATGM336H GPS, DS3231 RTC,
 SW-420 vibration sensor, DFRobot microSD module, SW-420 tamper
 sensor.
- **v5** (Ø 100 mm, in development). Everything above, plus a
 XIAO ESP32-C5 radio coprocessor, Waveshare UPS Module 3S (3S
 18650 + INA219 battery telemetry over I²C), Qwiic and 2×6 GPIO
 expansion connectors.

Both revisions use removable DIP sockets for every active component.
no soldering required to swap an MCU, the GPS module, or the Heltec
mesh node. V4 is 100% through-hole. V5 adds one SMD Qwiic
connector that hand-solders cleanly.

## Per-chip responsibilities

```
            ┌──────────────────────────────────────────┐
            │                Halberd v5                 │
            │                                           │
   USB ────►│  XIAO ESP32-S3 ◄──UART2──► XIAO ESP32-C5  │
            │   (Halberd FW)              (coprocessor) │
            │       │                            │       │
            │     UART1                        UART       │
            │       │                            │       │
            │   Heltec V3                     ATGM336H   │
            │  (Meshtastic)                     GPS      │
            │                                            │
            │   I²C: DS3231 RTC, INA219 (UPS)            │
            │   GPIO: SW-420 vibration, SD (SPI)         │
            │   C5 I²C: J_QWIIC, J_EXP                   │
            └──────────────────────────────────────────┘
                          │
                       LoRa mesh
                          │
                          ▼
            Other Halberd nodes + diginode-cc fleet server
```

| Function | S3 (main MCU) | C5 (coprocessor, v5 only) |
|---|---|---|
| Orchestration | **owns** | follower |
| Local web UI (`halberd-full`) | **owns** |. |
| Meshtastic mesh link | **owns** |. |
| SD card logging | **owns** |. |
| DS3231 RTC + INA219 telemetry (S3 I²C bus) | **owns** |. |
| Wi-Fi 2.4 GHz scanning | **owns** |. |
| Wi-Fi 5 GHz scanning |. | **owns** (stage 4) |
| 5 GHz promiscuous probe sniffer |. | **owns** (stage 8) |
| BLE 2.4 GHz scanning | **owns** | **mirror** (Coded PHY etc., S3 dedupes) |
| BLE long-range (Coded PHY) |. | **owns** (stage 5) |
| IEEE 802.15.4 (Zigbee/Thread/Matter) |. | **owns** (stage 6) |
| GPS (UART) | v4 only | v5 (stage 3 moved it from S3) |
| Expansion I²C + EXP_GPIO (Qwiic / J_EXP) |. | **owns** (stage 7) |
| OpenDroneID drone detection | **owns** |. |
| Triangulation aggregation | **owns** |. |

## Why a coprocessor

The ESP32-S3 is a capable main chip but has three blind spots for
the Halberd mission:

- **Wi-Fi 2.4 GHz only.** Modern enterprise APs hide most of their
 traffic on 5 GHz channels the S3 can't reach.
- **BLE 5 features incomplete.** No reliable LE Coded PHY → no
 long-range BLE adverts (e.g. Bluetooth Remote ID long-range
 variant).
- **No 802.15.4 radio.** Zigbee, Thread, Matter, OpenThread,
 Wireless HART. All invisible.

The ESP32-C5 (released 2024) is Espressif's first dual-band chip and
ships a full 802.15.4 PHY. It runs ESP-IDF v6.0.1 (not Arduino.
802.15.4 has no Arduino wrapper) and earns its keep as a
multi-radio scanner that also happens to own the GPS UART because
relocating GPS to the C5 freed the S3 pins for the inter-chip link.

## Integration surfaces

A Halberd node sits at the intersection of three protocols.

1. **S3 ↔ C5 link.** Full-duplex UART at 921600 baud, COBS-framed
 binary messages with CRC-16/CCITT-FALSE. Single source of truth:
 [`Halberd/shared/link_protocol.h`](https://github.com/karamble/halberd/blob/main/Halberd/shared/link_protocol.h).
 See [link protocol](protocols/link.md).
2. **Halberd ↔ Meshtastic peers + diginode-cc.** Line-oriented text
 frames over Meshtastic's TEXTMSG channel. Readable on the wire,
 dispatched by first-token command. See
 [mesh protocol](protocols/mesh.md).
3. **`halberd-full` HTTP API.** REST endpoints over the device AP
 for local control and data export. See [API reference](api-rest.md).

The mesh protocol is the integration surface that matters most for
external implementors: a diginode-cc-compatible fleet server can be
built against it without touching the firmware.

## Region and regulatory

Halberd is built for international deployment.

- **Country code default** is `"01"` (World Wi-Fi). Enables passive
 scan on channels 1–13 (2.4 GHz) + 36–64, 100–144 (5 GHz). Omits
 UNII-3 (149–165, US/CA/AU-only). A US operator can override the
 country code to pick up UNII-3.
- **DFS channels** are enabled for passive scan. Halberd never
 transmits, so DFS rules (which gate *transmit* on weather-radar
 bands) don't restrict listening. ESP-IDF still gates them on the
 country code. `"01"` enables them.
- **Power output**: the node never transmits during scanning. The
 Meshtastic mesh link does transmit on LoRa (sub-GHz, region-set)
 but that's the Heltec sidekick, not the Halberd MCU.

## See also

- [Firmware layout](firmware/layout.md). Three firmware trees +
 shared protocol library.
- [C5 coprocessor](firmware/c5-coprocessor.md). What runs on the
 second chip.
- [Design decisions](decisions.md). The rationale behind
 non-obvious choices.
- [Roadmap](roadmap.md). Forward-looking work.
