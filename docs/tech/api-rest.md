# REST API reference

`halberd-full` exposes ~70 HTTP endpoints over its Wi-Fi AP at
`http://192.168.4.1`. All endpoints live in
[`Halberd/full/src/network.cpp`](https://github.com/karamble/halberd/blob/main/Halberd/full/src/network.cpp)
and are registered with ESPAsyncWebServer.

> **Note:** `halberd-headless` has no HTTP server. Those nodes are
> driven entirely by the [mesh protocol](protocols/mesh.md).

## Transport + auth

- **Bind**: AP-only Wi-Fi interface. The node's default AP SSID is
 `Halberd`, password `antihunt3r123` (both configurable). Any
 device joined to the AP can reach every endpoint.
- **HTTPS**: not supported. There's no TLS termination on the device.
- **Authentication**: none. Treat the AP credentials as the only
 access gate.
- **CORS**: not relevant. The same-origin web UI is the only
 intended consumer. Programmatic clients connect to the AP directly.

## Endpoint categories

The routes group naturally by feature.

### Core

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Web UI home (`index.html` served from PROGMEM) |
| `GET` | `/config` | Get current device config (JSON) |
| `POST` | `/config` | Update device config (JSON body) |
| `GET` | `/diag` | Diagnostic snapshot (uptime, heap, mesh state) |
| `GET` | `/stop` | Stop any running scan / detection task |
| `GET` | `/export` | Download current configuration as a JSON file |
| `GET` | `/save` (POST) | Persist current runtime config to NVS |

### Scan / detection

| Method | Path | Purpose |
|---|---|---|
| `POST` | `/scan` | Start a target-hunt scan (`SCAN_START` equivalent) |
| `POST` | `/sniffer` | Start a sniffer-mode (device-scan + probe) job |
| `POST` | `/drone` | Start drone (OpenDroneID) detection |
| `GET` | `/drone/status` | Drone-scan state |
| `GET` | `/results` | Hits from the most recent target scan |
| `GET` | `/sniffer-cache` | Sniffer device cache |
| `GET` | `/probe-results` | Probe-mode device + SSID list |
| `GET` | `/deauth-results` | Deauth-mode capture list |
| `GET` | `/drone-results` | Drone detection list |
| `GET` | `/drone-log` | Drone capture history |
| `POST` | `/clear-results` | Wipe in-RAM result caches |

### Baseline anomaly detection

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/baseline/status` | Phase (learn / monitor), counts |
| `GET` | `/baseline/stats` | Aggregated statistics |
| `GET` | `/baseline/config` | Current parameters |
| `POST` | `/baseline/config` | Update parameters |
| `POST` | `/baseline/reset` | Discard the current baseline, re-learn |

### MAC randomization correlation

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/randomization-results` | Identity table |
| `GET` | `/randomization/identities` | Detailed per-identity view |
| `POST` | `/randomization/reset` | Wipe in-RAM identity state |
| `POST` | `/randomization/clear-old` | Prune stale identities |

### Triangulation

| Method | Path | Purpose |
|---|---|---|
| `POST` | `/triangulate/start` | Begin multi-node fix |
| `POST` | `/triangulate/stop` | Cancel |
| `GET` | `/triangulate/status` | In-flight state |
| `GET` | `/triangulate/results` | Last computed fix |
| `POST` | `/triangulate/calibrate` | RF-env calibration |
| `GET` | `/triangulate/nodes` | Participating-node list |

### Security / tamper / erase

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/erase/status` | Erase-policy state |
| `POST` | `/erase/request` | Initiate secure-erase challenge |
| `POST` | `/erase/cancel` | Abort an in-flight request |
| `GET` | `/secure/status` | Tamper + auto-erase combined view |
| `POST` | `/secure/abort` | Cancel any pending erase |
| `GET` | `/config/autoerase` | Auto-erase policy |
| `POST` | `/config/autoerase` | Update auto-erase policy |
| `POST` | `/vibration` | Toggle SW-420 tamper sensing |

### Battery + power

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/battery-saver` | State (on/off, minutes remaining) |
| `POST` | `/battery-saver` | Engage / disengage |

### Configuration

| Method | Path | Purpose |
|---|---|---|
| `GET` / `POST` | `/rf-config` | RF presets (Relaxed / Balanced / Aggressive / Custom) |
| `GET` | `/wifi-config` | AP credentials, channels |
| `GET` | `/node-id` | Node identifier |
| `POST` | `/node-id` | Change node identifier |
| `GET` | `/gps` | Last GPS fix |
| `GET` | `/sd-status` | SD card mount + usage |
| `POST` | `/api/time` | Push host's Unix time to RTC |
| `GET` | `/allowlist-export` | Download allowlist |
| `POST` | `/allowlist-save` | Upload allowlist |

### Mesh

| Method | Path | Purpose |
|---|---|---|
| `POST` | `/mesh` | Send arbitrary text frame to Heltec |
| `POST` | `/mesh-hb` | Toggle heartbeat |
| `POST` | `/mesh-hb-interval` | Set heartbeat minutes |
| `GET` | `/mesh-test` | Ping Heltec, confirm link |
| `GET` / `POST` | `/mesh-interval` | Get/set mesh rebroadcast interval |

### SD data explorer (raw log files)

These endpoints stream the JSONL log files straight from the SD
card. Useful for offline analysis without removing the card.

| Method | Path | File |
|---|---|---|
| `GET` | `/api/probes.jsonl` | `/probes.jsonl` |
| `GET` | `/api/deauth.jsonl` | `/deauth.jsonl` |
| `GET` | `/api/drones.jsonl` | `/drones.jsonl` |
| `GET` | `/api/vibrations.jsonl` | `/vibrations.jsonl` |
| `GET` | `/api/halberd.log` | `/halberd.log` (text) |
| `GET` | `/api/probedb` | `/probedb.jsonl` (persistent probe history) |
| `POST` | `/api/probedb/clear` | Reset the probe DB |

See [data formats](./user/data-formats.md) for the JSONL schemas.

## Request / response conventions

- **Content type**: `application/json` for JSON payloads,
 `text/plain` for results / status text, `application/octet-stream`
 for downloadable files.
- **POST body**: most config endpoints accept either form-encoded
 fields (`application/x-www-form-urlencoded`) or a JSON body.
 The dispatcher tries both.
- **Status codes**: standard. `200` for success, `400` for bad
 input, `409` when another scan is already running, `500` on
 internal errors.
- **Long-running operations**: `POST /scan`, `POST /sniffer`, etc.
 return immediately after the worker task is spawned. Poll
 `/results` or the matching `*_results` endpoint to see hits as
 they accumulate.

## Discovery

There's no built-in OpenAPI spec. The canonical reference is the
source file itself.
[`Halberd/full/src/network.cpp`](https://github.com/karamble/halberd/blob/main/Halberd/full/src/network.cpp)
 which is grep-friendly for `server->on(` registrations.

## Security caveats

- The HTTP server is bound to the AP-only interface. It isn't
 reachable from any other network. **Do not expose this port to
 the Internet** by any kind of bridge.
- The AP credentials are the only gate. Change them from the
 defaults before deploying.
- The mesh interface (`POST /mesh`) lets a connected client send
 arbitrary text frames over LoRa. Anyone with AP access can
 spam your mesh.

## See also

- User handbook: [Web UI tour](./user/web-ui.md) for the narrative
 walkthrough.
- [Mesh protocol](protocols/mesh.md). The parallel remote-control
 interface used by diginode-cc.
