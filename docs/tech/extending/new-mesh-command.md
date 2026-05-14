# Adding a mesh command

Mesh commands are line-oriented text frames dispatched in
`network.cpp` by their first token. Adding a new command is mostly
mechanical. The cross-cutting concerns (rate limiting, ACK shape,
status exposure) are the parts worth getting right the first time.

## Code surfaces to touch

1. **`Halberd/{full,headless}/src/network.cpp`**. Add a handler
 function `static void handleYourCommand(const String &command)`
 plus an `else if` line in the main dispatcher chain.
2. **`Halberd/{full,headless}/src/network.h`**. Only if your handler
 needs to expose state to other modules.
3. **`README.md`** "Mesh Commands" section. Add a row.
4. **`docs/user/commands.md`**. Add the entry there.
5. **`docs/tech/protocols/mesh.md`** registry. Add the row.
6. If the command starts a long-running mode, see
 [Adding a detection mode](new-detection-mode.md) for the task
 side.

Both `halberd-full` and `halberd-headless` share the dispatcher
intent but not the source file. Apply the same change to both
trees in the same commit. CI compiles both but doesn't catch a
parity gap by itself.

## Argument grammar

The established convention is `:`-separated:

```
COMMAND:<arg1>:<arg2>[:<arg3>]...
```

- No spaces inside arguments.
- Strings that might contain `:` (like MAC addresses) keep their
 colons. Split on the first N delimiters and treat the rest as
 the trailing argument.
- Trailing flag tokens (`FOREVER`, `+ALL`, `+PROBE`) are
 order-tolerant and may appear in any combination.

Use `handleProbeStart` (in `network.cpp`) as the parser template
 it walks tokens after the duration with this pattern:

```cpp
int cur = secsDelim;
while (cur > 0) {
    int next = params.indexOf(':', cur + 1);
    String tok = params.substring(cur + 1, next > 0 ? next : params.length());
    tok.trim();
    tok.toUpperCase();
    if (tok == "FOREVER") forever = true;
    else if (tok == "+ALL") broadcastAll = true;
    cur = next;
}
```

## ACK envelope

```cpp
sendToSerial1(nodeId + ": YOUR_COMMAND_ACK:STATUS", true);
```

Statuses:

| Status | When |
|---|---|
| `STARTED` | Long-running mode accepted, worker task spawned |
| `STOPPED` | Stop/cancel succeeded |
| `BUSY` | Another scan / detection task was already running |
| `INVALID` | Arguments parsed but were unusable |
| `OK` | Short query, value follows / done |
| `ERR:<reason>` | Generic failure with a hint |

Emit exactly **one** ACK per command. Results are their own KIND
frames. Don't conflate.

## Conventions

- **Idempotency**: where it makes sense, repeating the command
 should be safe (e.g. `HB_ON` while heartbeats are already on
 returns `OK`, not `ERR`).
- **Cooldowns / dedup**: if your command triggers alert-style
 result frames, apply per-target dedup so a flood of detections
 doesn't saturate the mesh.
- **Size cap**: stay under `MAX_MESH_SIZE` per outgoing frame.
 `if (msg.length() <= (size_t)MAX_MESH_SIZE) sendToSerial1(msg, true);`
 is the standard guard.
- **Permissioning**: there's currently no per-command auth. Any
 mesh peer can issue any command. If you're adding something
 destructive (e.g. Erase), consider requiring a challenge/response
 flow like `ERASE_REQUEST` → `ERASE_FORCE:<token>`.

## Cross-variant parity

`halberd-full` typically has both a mesh-command handler and an
HTTP route for the same operation. `halberd-headless` only has
the mesh-command handler. The cleanest pattern: write the handler
once (callable from both the mesh dispatcher and the HTTP route)
and reference it from both call sites.

In practice, the existing code duplicates a little. Don't refactor
that as part of your add. Keep your commit focused.

## Worked example. `PROBE_START`

```cpp
// In network.cpp
static void handleProbeStart(const String &command) {
    // 1. Parse args
    String params = command.substring(12);  // strip "PROBE_START:"
    int modeDelim = params.indexOf(':');
    int mode = params.substring(0, modeDelim > 0 ? modeDelim : params.length()).toInt();
    int secs = 60;
    bool forever = false;
    bool broadcastAll = false;
    // ... walk remaining tokens for FOREVER / +ALL

    // 2. Validate
    if (secs < 1 && !forever) secs = 1;
    if (secs > 86400) secs = 86400;
    if (mode < 0 || mode > 2) {
        sendToSerial1(nodeId + ": PROBE_ACK:INVALID", true);
        return;
    }

    // 3. Single-radio gate
    if (scanning || workerTaskHandle || blueTeamTaskHandle || triangulationActive) {
        sendToSerial1(nodeId + ": PROBE_ACK:BUSY", true);
        return;
    }

    // 4. Set globals + spawn worker
    currentScanMode = (ScanMode)mode;
    stopRequested = false;
    scanning = true;
    probeBroadcastAll.store(broadcastAll);
    xTaskCreatePinnedToCore(probeDetectionTask, "probedet", 8192,
        reinterpret_cast<void*>((intptr_t)(forever ? 0 : secs)),
        1, &workerTaskHandle, 1);

    // 5. ACK
    sendToSerial1(nodeId + ": PROBE_ACK:STARTED", true);
}

// In the dispatcher (main if/else chain):
else if (command.startsWith("PROBE_START:"))  handleProbeStart(command);
```

Stop handler is a one-liner:

```cpp
static void handleProbeStop(const String &command) {
    stopRequested = true;
    sendToSerial1(nodeId + ": PROBE_ACK:STOPPED", true);
}
```

## Pitfalls

- **String parsing is fragile**. `toInt()` returns 0 on bad input
 with no error signal. Validate ranges before using the value.
- **Dispatcher order matters**. The chain uses `startsWith` and
 the first match wins. If your new command's name prefixes an
 existing one (e.g. `SCAN_START` would catch `SCAN_STARTUP_FOO`),
 the existing one wins. Use distinctive prefixes.
- **`MAX_MESH_SIZE` enforcement**. The Heltec silently drops
 oversized frames. You don't get a bounce. Test with realistic
 argument lengths.

## See also

- [Adding a detection mode](new-detection-mode.md) for the task
 side when the command starts a long-running operation.
- [Mesh protocol](./protocols/mesh.md). The wire format your
 command and ACK frames slot into.
- User handbook: [Mesh command reference](././user/commands.md).
