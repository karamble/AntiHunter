# Build and flash

Toolchain setup, `Makefile` targets, port wrangling for the S3 and
C5. The S3 firmwares build with PlatformIO; the C5 firmware builds
with ESP-IDF v6.0.1. The `Makefile` hides the difference behind
parallel targets.

> 🚧 **Stub page.** Content lands in a follow-up commit, sourced
> from the per-session wiki `08-dev-environment.md` and the
> repo `Makefile`.

## What this page will cover

### Toolchains

- **PlatformIO** (S3 builds): install path, `pio` CLI sanity check,
  Arduino-ESP32 platform pin, the `platformio.ini` envs.
- **ESP-IDF v6.0.1** (C5 build): why this version is pinned, the
  recommended `uv`-driven install (Python 3.13 via
  python-build-standalone, fastest install path; pyenv and Debian
  backports are slower or unavailable).
- `IDF_PATH` environment variable — what the `Makefile` expects,
  how to override.

### `Makefile` targets

- `make build` / `make build-full` / `make build-headless` — S3 PIO
  builds.
- `make flash-full` / `make flash-headless` — S3 flash, default
  `XIAO_PORT=/dev/ttyACM0`.
- `make c5-build` / `make c5-flash` / `make c5-monitor` /
  `make c5-clean` — C5 IDF lifecycle, default
  `C5_PORT=/dev/ttyACM1`.
- `make heltec-flash` / `make heltec-flash-only` /
  `make heltec-config-only` / `make heltec-confirm-config` —
  Meshtastic on the Heltec V3.
- `make flash-unit` — one-shot XIAO + Heltec programming for a
  fully-built node.
- `make lint` / `make lint-full` / `make lint-headless` — cppcheck
  pass on both variants.

### Common gotchas

- Port assignment when both XIAOs are plugged in simultaneously
  (`/dev/ttyACM0` vs `/dev/ttyACM1`); how to disambiguate by USB
  serial.
- `idf.py` requiring bash (the `Makefile` shells out explicitly).
- The S3-side `post:scripts/set_rtc_time.py` hook that runs after
  flash and what it sets.

## See also

- [Layout](layout.md) for the tree structure.
- [Variants](variants.md) for the env definitions in
  `platformio.ini`.
