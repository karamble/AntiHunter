# Halberd user handbook

You have a Halberd node (or you're about to flash one), and you want
to deploy it, run scans, read results, and troubleshoot when
something doesn't work. This handbook is for you. It assumes no
firmware development experience. Only that you can plug a node into
a USB port and follow step-by-step instructions.

If you want to know **how** Halberd works internally, head to the
[technical handbook](./tech/README.md) instead.

## Start here

- [Getting started](getting-started.md). Flash a node, do the first-
 boot AP-mode configuration, pair it to your Meshtastic mesh.

## Operation modes

One page per detection / scan mode, each self-contained: purpose,
when to use it, the command syntax, what the results mean.

- [Target hunt](modes/target-hunt.md). Find a specific MAC.
- [Device scan](modes/device-scan.md). Enumerate everything nearby.
- [Probe-request detection](modes/probe-detect.md). Fingerprint
 clients by what they're probing for.
- [Baseline anomaly detection](modes/baseline.md). Learn the
 environment, alert on changes.
- [Drone Remote ID detection](modes/drone-detect.md). Passive
 OpenDroneID capture.
- [Deauth attack detection](modes/deauth-detect.md). Spot
 active 802.11 deauth/disassoc floods.
- [MAC randomization correlation](modes/randomization.md). Link
 rotated MACs back to one device.
- [Triangulation](modes/triangulation.md). Multi-node RSSI
 position fix.

## Reference

- [Mesh command reference](commands.md). Every command grouped by
 use case.
- [Web UI tour](web-ui.md). For `halberd-full` only.
- [Data formats](data-formats.md). What each SD log file contains.
- [Battery + power tuning](battery-power.md). Battery saver, RF
 presets, runtime estimates.
- [Security + tamper response](security.md). Auto-erase, remote
 secure-erase tokens.
- [Troubleshooting](troubleshooting.md). When things don't work.
