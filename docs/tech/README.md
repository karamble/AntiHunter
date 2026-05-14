# Halberd technical handbook

You're building on Halberd: contributing to the firmware, integrating
a node into a larger system, implementing the mesh protocol on your
own server, or doing protocol research. This handbook covers the
architecture, the on-the-wire formats, the build system, and how to
add new code without breaking existing consumers.

If you just want to **run** a Halberd, the
[user handbook](../user/README.md) is the right entry point.

## Start here

- [Architecture overview](architecture.md) — S3 + C5 + Heltec
  sidekick; how a Halberd talks to peers and to the diginode-cc
  fleet server.
- [Design decisions log](decisions.md) — the *why* behind non-obvious
  choices (ESP-IDF on the C5, COBS+CRC framing, single-radio
  scheduling, …).
- [Roadmap](roadmap.md) — what's planned, what's a candidate, what's
  intentionally out of scope.

## Firmware

- [Layout](firmware/layout.md) — three firmware trees plus the shared
  protocol library.
- [Variants](firmware/variants.md) — what differs between
  `halberd-full` and `halberd-headless`.
- [Build + flash](firmware/build.md) — PlatformIO + ESP-IDF setup,
  `Makefile` targets, lint, port wrangling.
- [C5 coprocessor](firmware/c5-coprocessor.md) — what the C5 does,
  how stages are organised on the `feat/c5-firmware` branch.

## Protocols

- [S3 ↔ C5 link protocol](protocols/link.md) — COBS framing, CRC-16,
  message type registry, payload layouts.
- [Mesh protocol](protocols/mesh.md) — halberd ↔ diginode-cc message
  format over Meshtastic.
- [OpenDroneID parsing](protocols/opendroneid.md) — ASTM F3411
  Remote ID structures Halberd decodes off the air.

## Extending

- [Adding a detection mode](extending/new-detection-mode.md)
- [Adding a mesh command](extending/new-mesh-command.md)
- [Adding a link message type](extending/new-link-message.md)

## Reference

- [REST API reference](api-rest.md) — every HTTP endpoint exposed by
  `halberd-full`.
