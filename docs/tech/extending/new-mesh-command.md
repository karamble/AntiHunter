# Adding a mesh command

Mesh commands are line-oriented text frames dispatched by token in
`network.cpp`. Adding a new command is mostly mechanical, but there
are a few cross-cutting concerns (rate limiting, ACK shape, status
exposure) worth getting right the first time.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## What this page will cover

### Code surfaces to touch

1. **`Halberd/{full,headless}/src/network.cpp`** — add a
   `handleYourCommand(const String &command)` and dispatch from the
   main if/else chain.
2. **`network.h`** if the command needs to share state with other
   modules.
3. **README.md** "Mesh Commands" section.
4. **`docs/user/commands.md`** and (if it backs a new mode)
   **`docs/user/modes/<your-mode>.md`**.

### Conventions

- **Argument syntax**: `:` separators, no spaces inside arguments,
  trailing token flags like `FOREVER` or `+ALL`.
- **ACK envelope**: `<NODE_ID>: <COMMAND>_ACK:<STATUS>`.
  `STATUS ∈ STARTED / BUSY / STOPPED / INVALID / OK / ERR:<reason>`.
- **Reply envelope**: free-form text, but stay under `MAX_MESH_SIZE`
  per frame.
- **Idempotency**: where it makes sense, repeating the command
  should be safe.
- **Cooldowns**: per-target dedup if your command triggers
  alert-style frames.

### Cross-variant parity

`halberd-full` and `halberd-headless` share `network.cpp` in
intent but not in fact — apply the same change to both source trees.
The CI build catches the cppcheck pass but not the parity gap, so
add to both at once.

### Worked example

A walkthrough of adding `PROBE_START` (stage 8 timeframe) — the
parser, the task spawn, the ACK pattern, the cooldown.

## See also

- [Adding a detection mode](new-detection-mode.md) when the command
  backs a long-running task.
- [Mesh protocol](../protocols/mesh.md) for the wire-format
  reference.
