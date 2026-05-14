# Build and flash

Toolchain setup, `Makefile` targets, port wrangling for the S3 and
C5. The S3 firmwares build with PlatformIO; the C5 firmware builds
with ESP-IDF v6.0.1. The `Makefile` hides the difference behind
parallel targets.

## Toolchain pins

| Tool | Version | Where it lives |
|---|---|---|
| ESP-IDF | **v6.0.1** | `~/go/src/github.com/espressif/esp-idf/` |
| Python (for ESP-IDF) | **3.13** | via `uv`, in `~/.local/share/uv/python/cpython-3.13-…/bin/` |
| `uv` (Python manager) | 0.11.14+ | `~/.local/bin/uv` |
| PlatformIO | whatever `pio` picks up locally | system / pyenv |
| Arduino-ESP32 platform | `espressif32@^6.9.0` | declared in `platformio.ini`, pulled by PIO |

ESP-IDF v6.0.1 is the first stable release with full ESP32-C5
support — see [decisions](../decisions.md) for the version-pin
rationale.

## First-time setup

```bash
# 1. Get uv + Python 3.13 (only if system Python < 3.10)
curl -LsSf https://astral.sh/uv/install.sh | sh
~/.local/bin/uv python install 3.13

# 2. Clone ESP-IDF at v6.0.1
git clone https://github.com/espressif/esp-idf.git \
  ~/go/src/github.com/espressif/esp-idf
cd ~/go/src/github.com/espressif/esp-idf
git checkout v6.0.1
git submodule update --init --recursive --jobs 4   # ~10-20 min, 2.5+ GB

# 3. Install ESP-IDF toolchain for esp32c5
export PATH="~/.local/share/uv/python/cpython-3.13-linux-x86_64-gnu/bin:$PATH"
./install.sh esp32c5   # downloads RISC-V toolchain, ~250 MB into ~/.espressif/

# 4. PlatformIO bootstraps itself on first `pio run`; no separate step.
```

The IDF install creates its own venv at `~/.espressif/python_env/...`
using whichever `python3` is on PATH at install time; `export.sh`
activates that venv directly so the system Python doesn't need to
change.

## `Makefile` targets

All driven from the repo root.

### XIAO ESP32-S3 (PlatformIO / Arduino)

```bash
make build              # build both halberd-full and halberd-headless
make build-full
make build-headless
make flash-full         # default XIAO_PORT=/dev/ttyACM0
make flash-headless     # override: make flash-full XIAO_PORT=/dev/ttyACM2
make lint               # cppcheck on both variants
make clean              # remove PIO build artifacts
```

### XIAO ESP32-C5 (ESP-IDF)

```bash
make c5-build           # idf.py build (sources export.sh from IDF_PATH)
make c5-flash           # default C5_PORT=/dev/ttyACM1
make c5-monitor         # idf.py monitor (Ctrl-] to exit)
make c5-clean           # idf.py fullclean
```

The `c5-*` recipes shell out to bash explicitly (`bash -c '. "$(IDF_PATH)/export.sh" && ...'`)
because `idf.py`'s `export.sh` isn't POSIX-sh-compatible. The rest of
the `Makefile` stays on plain sh.

`IDF_PATH` defaults to `$(HOME)/go/src/github.com/espressif/esp-idf`;
override on the command line:

```bash
make c5-build IDF_PATH=/elsewhere/esp-idf
```

### Heltec V3 (stock Meshtastic)

```bash
make heltec-flash               # flash + apply Halberd config
make heltec-flash-only          # flash only
make heltec-config-only         # config only
make heltec-confirm-config      # verify config
```

Defaults: `HELTEC_REGION=EU_868`, `HELTEC_VERSION=2.7.15.567b8ea`.

### Combined

```bash
make flash-unit         # flash-full + heltec-flash, one-shot
```

## Serial port conventions

| Device | Default port | How to tell apart |
|---|---|---|
| XIAO ESP32-S3 | `/dev/ttyACM0` | First USB-CDC device plugged in |
| XIAO ESP32-C5 | `/dev/ttyACM1` | Second one; or use `udevadm info` |
| Heltec V3 | `/dev/ttyUSB0` (CP2102 chip) | Different driver (CP210x), spot via `dmesg \| tail` |

If only the C5 is plugged in (e.g. you're iterating on C5 firmware
alone), it will enumerate as `/dev/ttyACM0` — override
`C5_PORT=/dev/ttyACM0` for flash/monitor in that case.

## Lint

```bash
make lint               # both variants
make lint-full
make lint-headless
```

Runs `cppcheck` with `--enable=all --std=c++17` and a curated
suppression list (`missingIncludeSystem`, `missingInclude`,
`unusedFunction`, etc.). Excludes `wifi.c` and `opendroneid.c`
because they're third-party-influenced and don't follow our
conventions cleanly.

Lint errors fail the build (`--error-exitcode=1`). Some pre-existing
warnings exist in the triangulation block and the `cand1`/`cand2`
ISR helpers — those are tracked separately and don't currently
block.

## Repo branch conventions

- `main` is the integration branch.
- Feature branches use lowercase `feat/...` (e.g. `feat/c5-firmware`).
- Commit subjects use a lowercase prefix: `c5: ...`,
  `firmware: ...`, `docs: ...`, `gitignore: ...`.
- One stage = one commit. Each stage stands on its own with
  verification.

## Repository layout (reference)

```
halberd/
├── Halberd/
│   ├── c5/          # C5 firmware (ESP-IDF)
│   ├── full/        # S3 firmware with web UI (PIO/Arduino)
│   ├── headless/    # S3 firmware mesh-only (PIO/Arduino)
│   └── shared/      # cross-firmware C code (link codec, protocol)
├── Dist/            # distribution artifacts (generated)
├── docs/            # this docs tree
├── hw/              # hardware
│   ├── pcb/         # KiCad project — gitignored, lives in user's env
│   │   └── docs/    # design notes (gitignored)
│   └── Prototype_STL_Files/
├── scripts/         # flash-heltec-meshtastic.sh, set_rtc_time.py
├── Makefile         # top-level orchestrator (S3 + C5 + Heltec)
├── platformio.ini   # halberd-full and halberd-headless envs
└── README.md
```

## See also

- [Layout](layout.md) — what each firmware tree contains.
- [Variants](variants.md) — `halberd-full` vs `halberd-headless`.
- [C5 coprocessor](c5-coprocessor.md) — what the C5 firmware does.
