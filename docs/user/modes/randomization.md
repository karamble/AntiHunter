# MAC randomization correlation

Modern phones (iOS, Android, Windows) rotate their MAC addresses
when probing to make tracking harder. Randomization-correlation mode
tries to **link those rotated MACs back to a single device** using
Information Element (IE) order signatures, channel sequencing,
timing, and SSID fingerprints.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- Long-running surveillance where MAC-only tracking fails.
- Counting unique people / devices in an area that has MAC-
  randomizing clients.
- Forensics on probe captures where the MACs are LAA-set
  (locally administered address bit) and obviously randomized.

## What this page will cover

- `RANDOMIZATION_START:<mode>:<seconds>[:FOREVER]` syntax.
- The IE-order signature: how the order of Information Elements in a
  probe request becomes a per-device fingerprint that survives MAC
  rotation.
- Channel sequencing: a device probing channels 1 → 6 → 11 in the
  same order each scan creates a temporal fingerprint.
- The decision rules for "these N MACs are the same device" —
  conservative defaults, false-positive risk.
- Output: a device-identity table where one identity may map to
  several historic MACs.
- Persistence across sessions via the probe DB.

## Worked example

A two-hour capture in a coffee shop, then a randomization correlation
pass identifies how many unique devices were actually present vs.
the inflated MAC count.

## See also

- [Probe-request detection](probe-detect.md) — the underlying
  capture this mode analyses.
- [Target hunt](target-hunt.md) for hunting *one known* device,
  vs. this mode's "count unknown devices" framing.
