# Design decisions log

The *why* behind non-obvious choices in the firmware and protocol.
Use this page when you're about to revise an area and want to know
what the original constraint was. Most "huh, why is it like this?"
moments are answered here.

## C5 firmware uses ESP-IDF, not Arduino

The ESP32-C5 has full 802.15.4 support via ESP-IDF's
`esp_ieee802154_*` API. Arduino-ESP32 has no equivalent wrapper. The
C5 also has tighter access to BLE 5 features (Coded PHY, extended
adv) through the native NimBLE host component than through the
Arduino-NimBLE bridge.

The C5 firmware is small and self-contained. `setup()`/`loop()`,
`Wire`, `Serial` aren't worth the build-system complexity they'd
add. We re-implement what we need on the IDF UART / I²C drivers.

Trade-off accepted: two build systems in one repo (PlatformIO for
S3 variants, ESP-IDF for C5). The `Makefile` hides this behind
`c5-*` targets.

## ESP-IDF v6.0.1 pinned (not v5.5 LTS)

User requested "latest and greatest". V6.0.1 was current and the
first stable release with full ESP32-C5 support. V5.5 LTS was the
fallback if Python 3.10+ wasn't available, but `uv` made the Python
install painless so we stayed on v6.0.1.

## Python 3.13 via `uv` (not pyenv, not Debian backports)

Debian 11 ships Python 3.9. IDF v6.0.1 needs ≥ 3.10. Options
considered:

- **`pyenv`**. Compiles from source, ~10 min, needs build deps.
- **Debian backports**. No Python ≥ 3.10 available.
- **`uv`**. Fetches python-build-standalone in ~30 s, no compile,
 no root. **Chosen.**

The IDF install (`./install.sh esp32c5`) creates its own venv at
`~/.espressif/python_env/...` using whichever `python3` is on PATH
at install time. `export.sh` activates that venv directly, so the
system `python3` doesn't need to change.

## COBS + CRC-16 for link framing (not SLIP, not JSON)

- **JSON** would work today but won't keep up with high-rate BLE
 adv / Wi-Fi mass-scan streams.
- **SLIP** has an escape character → larger receiver state machine.
- **COBS** gives constant 1-byte-per-254 overhead, a single sync
 byte (`0x00`), no escape sequences. Frame boundary = next `0x00`.
- **CRC-16/CCITT-FALSE** (XMODEM-style) is well-tested. Used
 without a lookup table. 8-bit-at-a-time loop fits in negligible
 space.

## Single-byte length field (max 255 B payload)

- 802.15.4 frames max out at 127 B raw. Structured detections fit
 in well under that.
- BLE adv data + scan response max ≈ 62 B. Structured records
 < 100 B.
- Wi-Fi scan results < 64 B (SSID 32 + BSSID 6 + metadata).
- 255 is plenty. Two bytes would be over-engineering.

## C5 decodes. Doesn't forward raw frames

User directive: "we do not deliver RAW frames and let diginode-cc
figure out the protocol! this will all be done on c5".

So the link carries:
- Wi-Fi scan results = parsed APs (BSSID, SSID, RSSI, channel,
 security mode, PHY modes).
- BLE = parsed adverts (addr, type, PHY, structured adv_data).
- 802.15.4 = parsed MAC frame + protocol-family classification
 (Zigbee / Thread / Matter / Unknown).

The S3 receives structured records, never raw bytes.

## GPS push-only at 1 Hz (not pull-on-demand)

- Matches the existing TinyGPS interface: the S3 reads cached
 `gpsLat` / `gpsLon` / `gpsValid` globals whenever it wants.
- 1 Hz is sufficient for triangulation, drone telemetry, RTC
 discipline.
- One-way push is simpler than request/reply for the dominant case.
- A pull message (`LINK_MSG_GPS_QUERY`) can be added later as a
 single new type if a use case appears. Costs nothing to defer.

## Net names from the S3's perspective

`C5_LINK_TX` is the wire on which the S3 *transmits* → lands on
the C5's RX pin. `C5_LINK_RX` is the C5's TX wire → S3's RX pin.

Confusing if you read the C5-side code first, but the convention
is documented in `kicad-tutorial-v5.md` §2.3 and in
`Halberd/c5/main/hardware.h`. Renaming would break the schematic
netlist.

## S3 keeps `gpsLat` / `gpsLon` globals as the public API

When the C5 took over GPS in stage 3, two approaches were on the
table:

- **Drop-in replacement**. `apply_gps_fix()` writes the same
 globals the rest of the firmware already reads. Zero touch on
 callers. **Chosen.**
- **Wrapper API**. `getGPSFix()` / `isGPSValid()` accessors.
 rewrite every caller. Cleaner long-term, but ~30 extra call-site
 changes for no functional gain.

The globals are mutex-protected and the diff is minimal.

## `initializeGPS()` retained as a function name

The function used to set up the GPS UART. Now it sets up the C5
link. Rename considered, kept for now: zero changes in `setup()`
and downstream call sites, tests/scripts that grep "Initializing
GPS." still find it. A rename can happen as a tidy-up commit
later, or never.

## UART2 on the S3 (not UART1)

- UART0 is the USB-CDC console (`ARDUINO_USB_CDC_ON_BOOT=1`).
- UART1 is the Heltec mesh link (taken since v4).
- UART2 was the GPS UART in v4. On v5, GPS moved to the C5 → UART2
 freed → becomes the C5 link.

Same physical UART, same physical pins, different peer.

## Boot codec selftest on the C5 (not on the S3 yet)

The C5 always boots before any wiring can be validated. The
selftest round-trips a known frame through `link_encode` →
`link_decoder_feed` and asserts byte-for-byte match. Catches codec
arithmetic regressions without needing an S3 peer.

The S3 doesn't yet have an equivalent. Drift is unlikely since the
codec lives in `Halberd/shared/`, used by both sides. If we ever
worry, mirroring the selftest is a small commit.

## Custom partition table on the C5 (3 MB factory)

ESP-IDF default single-app layout caps the factory partition at 1
MB. Stage 4 (Wi-Fi) alone produces a 912 KB binary. Later stages
would overflow.

3 MB factory leaves ~500 KB unallocated for a future SPIFFS / NVS
partition. The C5 module has 4 MB flash. ESP-IDF defaults to 2 MB
for the C5 target, so `sdkconfig.defaults` also sets
`CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`.

OTA partitions deferred. Two 3 MB OTA slots wouldn't fit. A 1+1+1
MB layout would. Worth revisiting once the firmware stabilises.

## C5 `main` component has no REQUIRES / PRIV_REQUIRES

The IDF `main` component gets implicit access to *every* other
IDF component when neither `REQUIRES` nor `PRIV_REQUIRES` is set.
Adding either silently narrows the set and breaks `driver/uart.h` /
`esp_flash.h` includes.

Stage 4 added `wifi.c`, which needs `esp_wifi`, `esp_netif`,
`esp_event`, `nvs_flash`. We tried both `REQUIRES` and
`PRIV_REQUIRES`. Both broke other includes. We settled on **omit
both** and rely on the implicit-everything rule. If `main` ever
needs to split into separate components, each split gets its own
explicit `REQUIRES`.

## 5 GHz channel set: UNII-1 + UNII-2A + UNII-2C, no UNII-3

| Sub-band | Channels | Where it's legal |
|---|---|---|
| UNII-1 | 36, 40, 44, 48 | everywhere |
| UNII-2A (DFS) | 52, 56, 60, 64 | everywhere (DFS) |
| UNII-2C (DFS) | 100–144 | everywhere (DFS) |
| UNII-3 | 149, 153, 157, 161, 165 | US / CA / AU only |

Default `C5_WIFI_5GHZ_CHANNELS` skips UNII-3. A US operator can
either patch the array or eventually flip a runtime country-code
config (deferred).

DFS is included because Halberd is passive-scan-only. DFS rules
apply to *transmit*, not receive. Country `"01"` on the C5 enables
passive DFS scan in IDF v6.0.1 with `ieee80211d` on.

## Single-radio scan jobs (not parallel)

BLE, Wi-Fi 2.4 GHz, and 802.15.4 share the C5's 2.4 GHz front-end.
ESP-IDF time-slices but in practice they can't run simultaneously.

The protocol accepts this: each `*_REQ` returns an immediate
status (`OK` / `BUSY` / `ERROR`) via the matching `*_DONE`, events
stream, then a final `DONE` event. The S3 orchestrates serial jobs.

User signed off: "single radio capability is not a concern, because
it issues single scan jobs with timeouts/starts/and stops. C5 is
just a support radio".

## One commit per stage

Each stage on `feat/c5-firmware` is one commit:

- 1. Project scaffold (`1684e25`)
- 2. UART link layer (`5b85fe2`)
- 3. GPS handover (`0a1e915`)
- 4. 5 GHz Wi-Fi scan mirror (`0e14416`)
- 5. BLE scan mirror with Coded PHY (`022eb21`)
- 6. 802.15.4 frame sniffer (`7c8e1d8`)
- 7. Expansion bus (`e3189f2`)
- 8. 5 GHz probe sniffer (`00443a1`, post-roadmap)

Each message documents scope, file-by-file changes, and build
verification. Lets us revert any single stage. Bisecting against
hardware regressions becomes feasible.

## `Halberd/shared/` (not duplicated headers)

- The codec is ~250 LOC of non-trivial bit-shuffling. We want the
 C5 and S3 to always be in sync on framing.
- Two copies would drift the first time one side gets edited.
- Both build systems pull from a sibling dir:
 - **IDF (C5)**: `INCLUDE_DIRS "../../shared"` +
 `../../shared/link_codec.c` in `SRCS`.
 - **PIO (S3)**: `build_src_filter = ...+<Halberd/shared/*>`,
 `build_flags = ...-IHalberd/shared`.

## Country code default `"01"` (World Wi-Fi)

Halberd ships internationally. Defaulting to `"US"` would enable
UNII-3 but lock out usage in EU / Japan / China. `"01"` gets every
world-safe channel and lets a US user opt into UNII-3 by build flag.

The S3 firmware has `#define COUNTRY "US"` in `hardware.h:11`. This
will be revisited when the C5 country setting gets wired through.

## DFS channels enabled by default for passive scan

Halberd never transmits. DFS rules apply to *transmit*. Receive-only
is unrestricted. ESP-IDF still gates DFS channels on the country
code in some scan modes. Country `"01"` (and `"US"`) both enable
passive DFS scan.

## See also

- [Roadmap](roadmap.md). Forward-looking work.
- [Link protocol](protocols/link.md). The framing decisions land
 on this page.
- [C5 coprocessor](firmware/c5-coprocessor.md). Why the C5
 exists, what it does.
