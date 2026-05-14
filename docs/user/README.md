# Halberd user handbook

You have a Halberd node (or you're about to flash one), and you want
to deploy it, run scans, read results, and troubleshoot when
something doesn't work. This handbook is for you. It assumes no
firmware development experience — only that you can plug a node into
a USB port and follow step-by-step instructions.

If you want to know **how** Halberd works internally, head to the
[technical handbook](../tech/README.md) instead.

## Start here

- [Getting started](getting-started.md) — flash a node, do the first-
  boot AP-mode configuration, pair it to your Meshtastic mesh.

## Operation modes

One page per detection / scan mode, each self-contained: purpose,
when to use it, the command syntax, what the results mean.

- [Target hunt](modes/target-hunt.md) — find a specific MAC.
- [Device scan](modes/device-scan.md) — enumerate everything nearby.
- [Probe-request detection](modes/probe-detect.md) — fingerprint
  clients by what they're probing for.
- [Baseline anomaly detection](modes/baseline.md) — learn the
  environment, alert on changes.
- [Drone Remote ID detection](modes/drone-detect.md) — passive
  OpenDroneID capture.
- [Deauth attack detection](modes/deauth-detect.md) — spot
  active 802.11 deauth/disassoc floods.
- [MAC randomization correlation](modes/randomization.md) — link
  rotated MACs back to one device.
- [Triangulation](modes/triangulation.md) — multi-node RSSI
  position fix.

## Reference

- [Mesh command reference](commands.md) — every command grouped by
  use case.
- [Web UI tour](web-ui.md) — for `halberd-full` only.
- [Data formats](data-formats.md) — what each SD log file contains.
- [Battery + power tuning](battery-power.md) — battery saver, RF
  presets, runtime estimates.
- [Security + tamper response](security.md) — auto-erase, remote
  secure-erase tokens.
- [Troubleshooting](troubleshooting.md) — when things don't work.
