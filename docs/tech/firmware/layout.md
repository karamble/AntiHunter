# Firmware layout

Three pieces of firmware ship with each Halberd unit, plus stock
Meshtastic on the Heltec sidekick. Each lives in its own subtree and
builds with its own toolchain.

> 🚧 **Stub page.** Content lands in a follow-up commit, sourced
> from the per-session wiki `03-firmware-layout.md`.

## What this page will cover

- The tree structure:
  ```
  Halberd/
  ├── full/        # halberd-full — S3 firmware with local web UI
  ├── headless/    # halberd-headless — S3 firmware, mesh + cloud only
  ├── c5/          # halberd-c5 — C5 coprocessor firmware (v5 only)
  └── shared/      # code shared between S3 and C5
  ```
- Per-tree quick facts: target chip, toolchain, source path, key
  libraries, build target, flash target, binary size.
- `Halberd/shared/`: the link codec (`link_codec.{h,c}`) and the
  protocol single-source-of-truth (`link_protocol.h`). How each
  build picks the shared sources up (IDF `INCLUDE_DIRS`, PIO
  `build_src_filter`).
- Heltec V3: not built from this repo. The
  `scripts/flash-heltec-meshtastic.sh` helper pulls a stock
  Meshtastic release binary and applies a config pairing it with
  the Halberd S3 over UART1.
- The build matrix on `feat/c5-firmware`: binary sizes for each
  variant, RAM / flash percentages, partition layout.

## See also

- [Variants](variants.md) — what differs between `halberd-full`
  and `halberd-headless`.
- [Build + flash](build.md) — toolchain setup, `Makefile` targets.
- [C5 coprocessor](c5-coprocessor.md) — what the C5 firmware does.
