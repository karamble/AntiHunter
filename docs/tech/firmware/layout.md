# Firmware layout

Three pieces of firmware ship with each Halberd unit, plus stock
Meshtastic on the Heltec V3 sidekick. Each lives in its own subtree
and builds with its own toolchain.

```
Halberd/
├── full/        # halberd-full      S3 firmware with local web UI
│   └── src/
├── headless/    # halberd-headless  S3 firmware, mesh + cloud driven only
│   └── src/
├── c5/          # halberd-c5        C5 coprocessor firmware (v5 only)
│   ├── main/
│   ├── CMakeLists.txt
│   ├── partitions.csv
│   └── sdkconfig.defaults
└── shared/      # code shared between S3 and C5
    ├── link_codec.{h,c}        # COBS + CRC framing
    └── link_protocol.h         # message types + payload structs
```

Plus, off-tree:

- **Heltec V3** runs stock Meshtastic (no Halberd code on it). The
  script `scripts/flash-heltec-meshtastic.sh` downloads a specific
  Meshtastic release and applies a configuration that pairs the
  Heltec with the Halberd S3 over UART1.

## halberd-full (S3, with web UI)

| | |
|---|---|
| Target chip | ESP32-S3 (Seeed XIAO ESP32-S3) |
| Toolchain | Arduino-ESP32 via PlatformIO |
| Env | `halberd-full` in `platformio.ini` |
| Source | `Halberd/full/src/*.{cpp,c,h}` + `Halberd/shared/*` |
| Notable libs | ESPAsyncWebServer, AsyncTCP, NimBLE-Arduino, RTClib, ArduinoJson |
| Binary | ~1.7 MB |
| Build | `make build-full` or `pio run -e halberd-full` |
| Flash | `make flash-full` (default `XIAO_PORT=/dev/ttyACM0`) |

Contains everything `halberd-headless` does, plus an
ESPAsyncWebServer-backed local control panel served on the device's
Wi-Fi AP. Use this variant for interactive deployments, bench testing,
or single-node demos.

## halberd-headless (S3, mesh/cloud only)

| | |
|---|---|
| Target chip | ESP32-S3 (Seeed XIAO ESP32-S3) |
| Toolchain | Arduino-ESP32 via PlatformIO |
| Env | `halberd-headless` in `platformio.ini` |
| Source | `Halberd/headless/src/*.{cpp,c,h}` + `Halberd/shared/*` |
| Notable libs | NimBLE-Arduino, RTClib, ArduinoJson (no ESPAsyncWebServer) |
| Binary | ~1.3 MB |
| Build | `make build-headless` or `pio run -e halberd-headless` |
| Flash | `make flash-headless` |

Same scanning + detection + triangulation code as `halberd-full`.
Configured entirely via Meshtastic mesh messages — no local web UI.
The right variant for fleet deployments where diginode-cc or another
node drives every operation.

## halberd-c5 (C5, coprocessor)

| | |
|---|---|
| Target chip | ESP32-C5 (Seeed XIAO ESP32-C5) |
| Toolchain | **ESP-IDF v6.0.1**, `idf.py` directly (no Arduino) |
| Source | `Halberd/c5/main/*.{c,h}` + `Halberd/shared/*` |
| Build | `make c5-build` (sources IDF `export.sh` from `IDF_PATH`) |
| Flash | `make c5-flash` (default `C5_PORT=/dev/ttyACM1`) |
| Monitor | `make c5-monitor` (`Ctrl-]` to exit) |
| Console | USB Serial/JTAG (separate from UART0, which is the S3 link) |
| Binary | ~1.27 MB after stage 8; 60% of the 3 MB factory partition free |

The C5 firmware talks ESP-IDF natively because 802.15.4 has no
Arduino-ESP32 wrapper and the latest BLE 5 features benefit from
direct native NimBLE host access. See
[C5 coprocessor](c5-coprocessor.md) for the per-module breakdown and
[decisions log](../decisions.md) for the toolchain rationale.

## Shared library — `Halberd/shared/`

Pure C, no platform dependencies. Both the IDF build (C5) and the
PIO builds (S3 variants) pull this in:

- **`link_codec.h` / `link_codec.c`** — COBS encoder + streaming
  decoder with CRC-16/CCITT-FALSE. ~250 LOC, table-free.
- **`link_protocol.h`** — `enum link_msg_type` and packed payload
  structs. Single source of truth, identical bytes on both sides.

How each build picks the sources up:

- **C5 (IDF)**: `Halberd/c5/main/CMakeLists.txt` adds
  `../../shared/link_codec.c` to `SRCS` and `../../shared` to
  `INCLUDE_DIRS`.
- **S3 (PIO)**: each env has `build_src_filter = ...+<Halberd/shared/*>`
  and `build_flags = ... -IHalberd/shared`.

The shared header is `extern "C"`-wrapped for C++ callers (Arduino).

## Heltec V3 (stock Meshtastic)

Not built from this repo. The script
`scripts/flash-heltec-meshtastic.sh` pulls a stock Meshtastic release
binary and applies a config that pairs the Heltec with the Halberd
S3 over UART1.

| Target | Command |
|---|---|
| flash + configure | `make heltec-flash` |
| flash only | `make heltec-flash-only` |
| configure already-flashed | `make heltec-config-only` |
| verify expected config | `make heltec-confirm-config` |

Defaults: `HELTEC_REGION=EU_868`, `HELTEC_VERSION=2.7.15.567b8ea`.

## Build matrix

After stage 8 (commit `00443a1`):

| Target | Binary | Size | Free |
|---|---|---:|---:|
| `halberd-c5` | `halberd_c5.bin` | 1.27 MB | 60% of 3 MB factory |
| `halberd-headless` | `firmware.bin` | 1.27 MB | RAM 16.3%, flash 39.9% |
| `halberd-full` | `firmware.bin` | 1.65 MB | RAM 16.5%, flash 51.6% |

The C5 partition was bumped from the IDF default (1 MB) to 3 MB in
stage 4 — the Wi-Fi stack alone produces a 912 KB binary; later
stages would overflow. The whole multi-radio + I²C/GPIO firmware
now lives in 1.27 MB with 1.73 MB of headroom for future stages or
OTA.

## See also

- [Variants](variants.md) — `halberd-full` vs `halberd-headless`
  in detail.
- [Build + flash](build.md) — toolchain setup and `Makefile`
  targets.
- [C5 coprocessor](c5-coprocessor.md) — what the C5 firmware
  actually does, stage-by-stage.
