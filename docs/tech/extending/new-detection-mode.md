# Adding a detection mode

Halberd's detection modes are FreeRTOS tasks orchestrated by a
mesh command + (on `halberd-full`) a web-UI handler. This page is
the recipe for adding a new one without breaking the existing
modes or the radio scheduling.

> 🚧 **Stub page.** Content lands in a follow-up commit. The
> stage 8 commit `00443a1` (5 GHz probe sniffer) is the most recent
> concrete example.

## What this page will cover

### Code surfaces to touch

1. **`scanner.cpp`** (or a new sibling file) — the task body.
2. **`network.cpp`** — the mesh-command handler that spawns the
   task, plus the corresponding HTTP route handler on
   `halberd-full`.
3. **`hardware.h`** / wherever your mode-specific state lives.
4. **README.md** "Mesh Commands" section — the user-facing entry.
5. **`docs/user/modes/<your-mode>.md`** — narrative documentation.

### Patterns to follow

- Single-radio scheduling: if your mode uses Wi-Fi promiscuous or
  BLE scan, gate it against the existing `scanning` /
  `workerTaskHandle` flags so concurrent modes return BUSY rather
  than corrupt each other.
- `mode:secs[:FOREVER]` argument shape — matches existing modes,
  minimal new parser code.
- ACK envelope: emit `<NODE>: <MODE>_ACK:STARTED` / `BUSY` /
  `STOPPED` / `INVALID` consistently.
- SD logging: open the log file once, append JSONL, rotate at
  ~1 MB.
- Mesh dedup: hold per-target cooldowns to avoid flooding the
  mesh on rapid-fire detections.

### Pitfalls

- The Heltec mesh-link UART is shared with the heartbeat path —
  flooding it back-pressures heartbeats and ACKs.
- The S3 `ProbeRequestQueue` (and similar) live on the heap; on
  failed `xQueueCreate` the task body silently no-ops.
- Mode-specific globals must reset cleanly on task exit, or a
  subsequent run inherits stale state.

### Worked example

Lift from the stage 8 commit (`00443a1`) — adding the 5 GHz
probe-request sniffer — as the reference walkthrough.

## See also

- [Adding a mesh command](new-mesh-command.md) for the dispatcher
  side of any new mode.
- [Adding a link message type](new-link-message.md) if your mode
  needs the C5's help.
