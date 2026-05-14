# `halberd-full` vs `halberd-headless`

The two S3-side firmware variants share their scanning, detection,
and triangulation code — they differ only in whether they ship a
local HTTP server with a web control panel. This page lays out what
differs and how to pick one for a deployment.

## What's the same

Both variants:

- Run on the **same XIAO ESP32-S3 hardware**, fully interchangeable
  at flash time.
- Run **all the same detection modes** — target scan, device scan,
  probe detection, baseline anomaly, drone RID, deauth, MAC
  randomization correlation, triangulation.
- Speak the **same Meshtastic mesh-command protocol**. A node
  flashed with either variant responds to `STATUS`, `SCAN_START`,
  `PROBE_START`, etc., identically.
- Speak the **same S3 ↔ C5 link protocol** on v5 hardware. The C5
  doesn't know or care which S3 variant is on the other end.
- Persist **the same configuration in NVS**. Re-flashing a node
  from one variant to the other preserves its node ID, AP
  credentials, targets, RF preset, etc.
- Log **the same SD-card data formats** (`probes.jsonl`,
  `deauth.jsonl`, `drones.jsonl`, `vibrations.jsonl`,
  `halberd.log`, `probedb.jsonl`).

## What differs

| Feature | halberd-full | halberd-headless |
|---|:---:|:---:|
| Local HTTP server (ESPAsyncWebServer + AsyncTCP) | ✅ | — |
| Web UI HTML / JS / CSS | ✅ | — |
| REST API on the device AP | ✅ | — |
| Mesh-command dispatcher | ✅ | ✅ |
| Scanning + detection code | ✅ | ✅ |
| Triangulation | ✅ | ✅ |
| SD logging | ✅ | ✅ |
| Binary size | ~1.65 MB | ~1.27 MB |
| RAM usage | ~16.5% | ~16.3% |
| Flash usage | ~51.6% | ~39.9% |

The size delta is the cost of `ESPAsyncWebServer` + `AsyncTCP` +
the embedded UI HTML.

## When to use which

Pick **`halberd-full`** when:

- You're benching a single node and want to drive it interactively.
- You're giving a demo and a phone-as-AP-client is the right UX.
- You're commissioning a node and want to do the initial configure
  through a browser at `http://192.168.4.1`.
- You're debugging scan behaviour and want to watch live results in
  a UI rather than parsing mesh frames.

Pick **`halberd-headless`** when:

- You're deploying a fleet of nodes driven by diginode-cc.
- The node will live unattended where no one will ever join its AP.
- You want maximum flash headroom for future feature growth.
- Battery life is tight and you'd rather not have an idle HTTP
  server eating cycles even when no one's connected.

## PlatformIO env definitions

Both envs are declared in `platformio.ini` at the repo root. The
relevant differences:

- **Source tree**: `halberd-full` builds from `Halberd/full/src/`;
  `halberd-headless` builds from `Halberd/headless/src/`.
- **Library deps**: `halberd-full` adds `ESPAsyncWebServer-esp32`
  + `AsyncTCP-esp32` on top of the shared list.
- **Build flag**: each env defines its own
  `-D HALBERD_VARIANT_*` macro so shared code can branch on it
  when needed.

Both envs:

- Target `seeed_xiao_esp32s3`.
- Run at 115200 monitor baud.
- Hook `post:scripts/set_rtc_time.py` after a successful flash
  to push the host's current Unix time to the device RTC.

## Switching variants on a deployed node

Re-flashing a node from one variant to the other:

```bash
# from halberd-full to halberd-headless:
make flash-headless

# or the other way:
make flash-full
```

Configuration in NVS survives — node ID, AP credentials, targets,
RF preset, heartbeat interval, all preserved. The SD card is
untouched. The DS3231 RTC keeps its time (it has its own coin cell).

You can flash either variant via the [web flasher](../../index.html);
the flasher's variant selector exposes both options.

## See also

- [Layout](layout.md) — both variants in the larger firmware tree.
- [Build + flash](build.md) — `make` targets for each.
- User handbook: [Web UI tour](../../user/web-ui.md) — what
  `halberd-full` adds that `halberd-headless` doesn't.
