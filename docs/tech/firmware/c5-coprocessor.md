# C5 coprocessor

The Seeed XIAO ESP32-C5 is the second MCU on a v5 Halberd carrier.
It runs ESP-IDF v6.0.1 natively (no Arduino) and owns 5 GHz Wi-Fi,
BLE 5 (1M + 2M + Coded PHY), IEEE 802.15.4, the GPS UART, and the
I²C / EXP_GPIO expansion bus. The S3 talks to it over a 921600-baud
UART link with COBS-framed binary messages.

## Why the C5

The ESP32-S3 is a capable main chip but has three blind spots:

- **Wi-Fi 2.4 GHz only.** Misses 5 GHz APs entirely.
- **BLE 5 incomplete.** No reliable LE Coded PHY (long-range
 adverts, including Bluetooth Remote ID's long-range variant).
- **No 802.15.4 radio.** Zigbee, Thread, Matter, OpenThread,
 Wireless HART. All invisible.

The ESP32-C5 (released 2024) is Espressif's first dual-band chip:

- **2.4 + 5 GHz Wi-Fi 6**
- **BLE 5** with LE Coded PHY, 2M PHY, extended advertising
- **IEEE 802.15.4** radio (shares the 2.4 GHz front-end with
 Wi-Fi/BLE but addressable independently via
 `esp_ieee802154_*`)

Plus a GPS UART, an I²C bus, and five GPIOs that the S3 doesn't
have spare pins for after the v4 carrier was laid out.

So the C5 isn't a GPS coprocessor that happens to scan radios. It's
a multi-radio scanner + sensor hub that happens to own the GPS UART
because moving GPS off the S3 freed the two pins we needed for the
inter-chip link.

## Why ESP-IDF, not Arduino

- 802.15.4 has no Arduino-ESP32 wrapper. `esp_ieee802154_*` only
 exists in IDF.
- IDF v6.0.1 is the first stable release with full ESP32-C5
 support.
- The C5 firmware is small and self-contained. Arduino's
 setup/loop / Wire / Serial conveniences aren't worth the
 build-system complexity. We re-implement what we need with
 IDF UART/I²C drivers directly.

Trade-off accepted: two build systems in one repo. The `Makefile`
hides this behind `c5-*` targets.

## Division of responsibilities

| Function | S3 | C5 |
|---|---|---|
| Orchestration | **owns** | follower |
| Local web UI (`halberd-full`) | **owns** |. |
| Meshtastic link (to Heltec) | **owns** |. |
| SD card storage | **owns** |. |
| DS3231 RTC + INA219 (S3 I²C bus) | **owns** |. |
| Wi-Fi 2.4 GHz scanning | **owns** |. |
| Wi-Fi 5 GHz scanning |. | **owns** (stage 4) |
| 5 GHz promiscuous probe sniffer |. | **owns** (stage 8) |
| BLE 2.4 GHz scanning | **owns** | **mirror**. S3 dedupes |
| BLE long-range (Coded PHY) |. | **owns** (stage 5) |
| 802.15.4 (Zigbee / Thread / Matter) |. | **owns** (stage 6) |
| GPS reader |. | **owns** (stage 3) |
| Expansion I²C + EXP_GPIO |. | **owns** (stage 7) |
| OpenDroneID drone detection (Wi-Fi NAN) | **owns** |. |
| Triangulation aggregation | **owns** |. |

### Rules of engagement

1. **S3 issues commands. C5 executes.** The C5 doesn't initiate
 scans on its own. The one exception is the always-on GPS reader
 . GPS is a passive device that emits NMEA continuously, so the
 C5 reader is trivial.
2. **Single-radio scheduling on the C5.** BLE, Wi-Fi 2.4 / 5 GHz,
 and 802.15.4 share the same RF front-end. Only one radio job at
 a time. A `*_REQ` arriving while another scan runs returns
 `*_DONE` with `status = BUSY` and the S3 retries.
3. **C5 decodes. Doesn't forward raw bytes.** Wi-Fi scan results
 are parsed APs, BLE adverts are parsed structured records,
 802.15.4 frames are MAC-layer parsed + protocol-family
 classified. The S3 never receives raw NMEA or raw PHY frames.
4. **S3 dedupes BLE overlap.** Both chips can see 2.4 GHz BLE. The
 S3 dedupes by `(addr, time window)` so a single device doesn't
 show up twice. C5-only adverts (Coded PHY, extended adv) pass
 through naturally because they have no S3 counterpart.

## Stage history (commits on `feat/c5-firmware`)

| # | What | Commit |
|---:|---|---|
| 1 | Project scaffold (ESP-IDF v6.0.1 bring-up) | `1684e25` |
| 2 | UART link layer (COBS + CRC framing) | `5b85fe2` |
| 3 | GPS handover from S3 to C5 | `0a1e915` |
| 4 | 5 GHz Wi-Fi scan mirror | `0e14416` |
| 5 | BLE scan mirror (1M + Coded PHY, ext-adv) | `022eb21` |
| 6 | IEEE 802.15.4 sniffer + protocol decode | `7c8e1d8` |
| 7 | Expansion bus (I²C + EXP_GPIO over link) | `e3189f2` |
| 8 | 5 GHz promiscuous probe-request sniffer | `00443a1` |

Each stage is one commit, individually revertable. Commit messages
document file-by-file changes, build sizes, and verification.

## Module map. `Halberd/c5/main/`

| File | Purpose |
|---|---|
| `main.c` | Boot banner + chip info, calls each module's `init` in order |
| `hardware.h` | Pin assignments, UART numbers, baud rates |
| `link.{h,c}` | UART driver, COBS+CRC framer, message-type dispatcher |
| `gps.{h,c}` | NMEA parser, 1 Hz `LINK_MSG_GPS_FIX` push |
| `wifi.{h,c}` | Active-scan workflow for 5 GHz |
| `wifi_sniff.{h,c}` | Promiscuous-mode probe-request sniffer (stage 8) |
| `ble.{h,c}` | NimBLE observer, 1M + Coded PHY scans, ext-adv |
| `ieee802154.{h,c}` | Promiscuous 802.15.4 RX + protocol classifier |
| `exp.{h,c}` | Synchronous I²C read/write + EXP_GPIO config/read/write |

Plus `Halberd/shared/` for the protocol header and codec.

## Boot init order

`main.c`'s `app_main()` calls the module inits in this order:

```c
link_init();          // UART driver + framer + dispatcher
gps_init();           // GPS UART reader, 1 Hz push
wifi_init();          // Wi-Fi stack in scan-only mode
wifi_sniff_init();    // Promiscuous-mode probe sniffer (stage 8)
ble_init();           // NimBLE observer
ieee802154_init();    // 802.15.4 promiscuous RX
exp_init();           // I²C + EXP_GPIO
```

Order matters: `wifi_sniff_init()` after `wifi_init()` because the
sniff task shares `wifi_radio_mutex` (created by `wifi_init`).
After all inits, a 30 s loop emits `LINK_MSG_STATUS` heartbeats.

## Region and regulatory

- **Country code default**: `"01"` (World Wi-Fi). Enables passive
 scan on channels 1–13 (2.4 GHz) + 36–64, 100–144 (5 GHz). Omits
 UNII-3 (149–165, US-only).
- **DFS channels** are enabled for passive scan. Halberd never
 transmits, so DFS rules don't restrict listen.
- **ieee80211d on**: the radio honours the country code on DFS
 bands per the standard.

## Power + coexistence

- 5V rail: both S3 and C5 draw from the same `+5V` bus, sourced
 from the Waveshare UPS Module 3S on v5.
- 3V3 rail: unified. Both the S3 LDO and the C5 LDO feed the same
 rail in parallel. Either can source.
- The C5 has its own U.FL antenna connector for 5 GHz / 802.15.4.
 chassis mount via pigtail, distinct from S3 and Heltec antennas.

## See also

- [S3 ↔ C5 link protocol](./protocols/link.md). The wire format
 that drives this whole thing.
- [Architecture](./architecture.md). The C5's place in the bigger
 picture.
- [Decisions log](./decisions.md). Why the C5, why ESP-IDF, why
 the partition layout.
- [Roadmap](./roadmap.md). Beyond-stage-8 candidates.
