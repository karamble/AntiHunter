# Adding a detection mode

A "detection mode" in Halberd is a long-running FreeRTOS task
spawned by a mesh command (and, on `halberd-full`, an HTTP route)
that scans some radio, captures interesting frames / events, and
emits results to the mesh + the SD card. This page is the recipe
for adding a new one without breaking existing modes or the radio
scheduling.

The most recent worked example is stage 8 (commit `00443a1`),
which added 5 GHz probe-request sniffing wired into the existing
probe mode. Read that commit for the concrete pattern.

## Anatomy of an existing mode

Take `PROBE_START` (probe-request detection) as the template:

```
network.cpp                 scanner.cpp                  SD + mesh
─────────────                ─────────────                ─────────
handleProbeStart            probeDetectionTask()         /probes.jsonl
   │  parse args               │  set up promiscuous       │
   │  spawn task               │  drain probeRequestQueue  │
   │  emit ACK                 │  per-event handler:       │
   ▼                           │    processProbeRequest...
[STARTED ACK on mesh]          │    → probeDevices map
                               │    → sendProbeHitMesh()
                               │    → SD append on shutdown
                               ▼
                            PROBE_DONE ACK
```

Each piece is replaceable: the parser, the task body, the per-event
handler, the result aggregator.

## Code surfaces to touch

1. **`Halberd/{full,headless}/src/scanner.cpp`** (or a new sibling
 file). The task body itself (`void yourModeTask(void *pv)`).
2. **`Halberd/{full,headless}/src/scanner.h`**. Extern-declare any
 globals your task exposes (`extern std::atomic<bool>
 yourModeEnabled`).
3. **`Halberd/{full,headless}/src/network.cpp`**. Add
 `static void handleYourModeStart(const String &command)` and a
 dispatch line in the main if/else chain.
4. **`Halberd/full/src/network.cpp`** HTTP server section. Add an
 `server->on("/your-mode", HTTP_POST, ...)` route that calls the
 same handler.
5. **`README.md`** "Mesh Commands" section. Add a row.
6. **`docs/user/modes/<your-mode>.md`**. Narrative documentation.
7. **`docs/tech/protocols/mesh.md`** command registry. Add the
 row.

Both halberd-full and halberd-headless need the parser change. CI
builds both but won't catch a parity gap by itself. Apply the
patch to both source trees in the same commit.

## Patterns to follow

### Single-radio scheduling

Halberd has a small set of "I am using the radio now" flags:

```cpp
extern std::atomic<bool> scanning;
extern TaskHandle_t workerTaskHandle;
extern TaskHandle_t blueTeamTaskHandle;
extern std::atomic<bool> triangulationActive;
```

The dispatch handler should check **all of them** before spawning
your task:

```cpp
if (scanning || workerTaskHandle || blueTeamTaskHandle || triangulationActive) {
    sendToSerial1(nodeId + ": YOUR_MODE_ACK:BUSY", true);
    return;
}
```

Your task sets `scanning = true` on entry and `false` on exit.
Failing to do this lets two modes try to drive the same radio,
which results in everything from corrupted scans to driver panics.

If your mode uses the C5 (5 GHz, BLE Coded PHY, 802.15.4, expansion
bus), the C5 side has its own `wifi_radio_mutex` that gates active
scans vs promiscuous sniffs. The S3 doesn't need to know. The C5
returns `*_DONE:BUSY` if it can't take the job.

### Command argument shape

The convention is `:`-separated:

```
COMMAND:<mode>:<seconds>[:FOREVER][:<extra-flag>]
```

- `mode` ∈ `{0=Wi-Fi, 1=BLE, 2=both}` if your mode is radio-
 selectable. Skip if not applicable.
- `seconds` is the duration. `FOREVER` overrides and runs until
 `STOP`.
- Trailing tokens are positional flags (`FOREVER`, `+ALL`,
 `+PROBE`, etc.). Order-tolerant.

Use the existing `handleProbeStart` as a parser template. It
handles the FOREVER + +ALL flag combination and is the most recent
example.

### ACK envelope

```cpp
sendToSerial1(nodeId + ": YOUR_MODE_ACK:STARTED", true);
// or BUSY / STOPPED / INVALID
```

Send exactly one ACK per command. Don't ACK on every result frame
 results are their own KIND (see below).

### Result frames

Stream results as `<NODE_ID>: YOUR_KIND ...` frames. Conventions:

- Key=value tokens, whitespace-separated.
- One target = one frame.
- Apply per-target dedup so a flood of detections doesn't saturate
 the mesh. Existing modes use 3 s (hits), 60 s (probe hits).
 pick what makes sense.
- Stay under `MAX_MESH_SIZE` per frame. If a result is too long,
 trim rather than splitting.

### SD logging

Open the log file once at task start, append JSONL records, close
on task exit. Pattern:

```cpp
File log = SD.open("/your-mode.jsonl", FILE_APPEND);
if (log) {
    DynamicJsonDocument doc(512);
    doc["t"] = getEventTimestamp();
    doc["mac"] = macStr;
    // ...
    serializeJson(doc, log);
    log.println();
    log.close();
}
```

Rotate at ~1 MB to `_old.jsonl` (existing modes do this). Replace
the old rotation file every time.

### Forever mode + STOP

The task body's main loop checks two conditions:

```cpp
while ((forever && !stopRequested) ||
       (!forever && (millis() - startTime) < (uint32_t)(duration * 1000) && !stopRequested)) {
    // ...
}
```

`stopRequested` is a file-global `std::atomic<bool>` toggled by the
`STOP` command. Reset it to `false` at the start of your task.

### Reset state cleanly on exit

Your task must leave all globals it touches in a known-clean state
on exit, so a subsequent run doesn't inherit stale data. Typical
cleanup:

- Clear in-RAM caches if your mode is one-shot.
- Stop the radio (call `radioStopSTA()` / `radioStopBLE()` as
 appropriate).
- Clear `scanning` and any mode-specific flags.
- Flush pending SD writes.

## Pitfalls

- **The Heltec mesh UART is shared with heartbeats**. Flooding it
 back-pressures heartbeats and ACKs. Use per-target dedup.
- **Queue `xQueueCreate` can fail under tight RAM**. Check the
 return value. Degrade gracefully (`scanning = false` + emit
 `YOUR_MODE_ACK:INVALID`) rather than calling `xQueueSend` on a
 null handle.
- **Mode-specific globals leak across runs**. Always reset them
 at task start, not at task end (a crash mid-task otherwise
 leaves them dirty).
- **HTTP route and mesh-command parity**. If you add an HTTP route
 on `halberd-full` but no mesh command, the headless variant
 can't trigger your mode. Diginode-cc-driven deployments lose
 it. Always pair them.

## Worked example (stage 8)

Adding the 5 GHz probe-request sniffer (commit `00443a1`):

- `network.cpp`: existing `handleProbeStart` was already there. No
 new parser. The sniffer slotted into the running `probeDetectionTask`.
- `scanner.cpp`: the inner-loop body was extracted into a new
 `processProbeRequestEvent()` helper so a new C5-side drain
 (`drainC5WifiProbes()`) could feed the same dedup / hit /
 mesh-broadcast pipeline as the existing 2.4 GHz ISR queue.
- A 10 s re-arm loop trigger called `triggerC5WifiProbeSniff()`
 every 10 s with a 9 s sniff window.
- No new mesh command. Extending an existing mode rather than
 adding a peer.

That's the simplest possible new-mode addition: piggyback on an
existing command and extend its capture surface.

A fresh, standalone mode adds the full set of files listed in
[Code surfaces](#code-surfaces-to-touch).

## See also

- [Adding a mesh command](new-mesh-command.md). The parser side
 of a new mode.
- [Adding a link message type](new-link-message.md). When your
 mode needs the C5 to do work.
- [Mesh protocol](./protocols/mesh.md). The wire format your ACK
 and result frames slot into.
