# Security and tamper response

Halberd is built to fail safely when it's been tampered with or when
its location is compromised. This page covers the tamper detection
chain, the auto-erase policy, and the remote secure-erase token
mechanism. Including how to test them without bricking your node.

> 🚧 **Stub page.** Content lands in a follow-up commit.

## What this page will cover

- The SW-420 vibration sensor: what it detects (board movement, drop,
 case-open if mounted on the lid), what it doesn't (slow tilt,
 thermal stress).
- `VIBRATION_ON` / `VIBRATION_OFF` / `VIBRATION_STATUS`. Runtime
 control of tamper sensing.
- `AUTOERASE_ENABLE` / `AUTOERASE_DISABLE` / `AUTOERASE_STATUS`.
 the "if tampered, wipe" policy. Default is **disabled**.
- The secure-erase token: how it's provisioned, where it's stored,
 what `ERASE_REQUEST` / `ERASE_FORCE:<token>` / `ERASE_CANCEL`
 actually erase (NVS, SD partitions, both?).
- What survives an auto-erase (firmware, RTC time, GPS module) vs.
 what doesn't (targets, logs, baseline DB, config).
- Testing tamper response on the bench without losing your real data.
- Privacy Mode: redacting MAC last-octets / SSIDs from result pages
 before screenshotting.

## Threat model assumptions

What the security features are designed to defend against, and what
they explicitly do not protect against (e.g. cold-boot attack, JTAG
on the XIAO, physical SD-card extraction before tamper trips).

## See also

- [SECURITY.md](././SECURITY.md). Responsible-disclosure policy.
- [Mesh command reference](commands.md). `ERASE_*` syntax.
