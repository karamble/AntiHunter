# Halberd C5 firmware

ESP-IDF firmware for the XIAO ESP32-C5 coprocessor on the Halberd v5 carrier
(`U7`). The C5 owns 5 GHz Wi-Fi scanning, BLE long-range, IEEE 802.15.4
(Zigbee/Thread/Matter detection), the GPS UART, and the Qwiic / J_EXP
expansion bus. The S3 (`U1`, `Halberd/headless` or `Halberd/full`)
orchestrates; the C5 executes and reports results over the UART link.

Stage 1 (this commit): toolchain bring-up only. The firmware boots, prints a
banner to USB Serial/JTAG, and heartbeats once per second. No protocol code,
no radios initialised yet.

## Prerequisites

ESP-IDF **v6.0.1** installed at `~/go/src/github.com/espressif/esp-idf/`. The
Makefile targets at the repo root source `export.sh` from that path before
invoking `idf.py`.

Override the path with `IDF_PATH=/your/path make c5-build` if your checkout
lives elsewhere.

## Build / flash / monitor

From the repo root:

```bash
make c5-build       # idf.py build
make c5-flash       # idf.py flash, default port /dev/ttyACM1
make c5-monitor     # idf.py monitor
make c5-clean       # idf.py fullclean
```

Override the serial port with `C5_PORT=/dev/ttyACMx`. If both the S3 and the
C5 are plugged in via USB, the S3 usually enumerates as `/dev/ttyACM0` and
the C5 as `/dev/ttyACM1` — but order depends on plug-in sequence. Use
`dmesg | tail` or `udevadm info` to disambiguate.

## Layout

```
Halberd/c5/
├── CMakeLists.txt         # IDF project file
├── sdkconfig.defaults     # target=esp32c5, USB Serial/JTAG console
├── README.md
└── main/
    ├── CMakeLists.txt     # main component registration
    ├── hardware.h         # pin defs from rev2.kicad_sch
    └── main.c             # app_main: boot banner + heartbeat
```

See `hw/pcb/docs/kicad-tutorial-v5.md` Appendix B for the C5 pin map and
`hw/pcb/docs/schematic.md` for the v5 netlist.
