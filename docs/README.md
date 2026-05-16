# Halberd documentation

Halberd is a self-contained tactical SIGINT mesh sensor. A small,
battery-powered ESP32-based node that scans Wi-Fi, BLE, and (on the
v5 hardware) 802.15.4, hunts targets by MAC, detects deauth attacks
and OpenDroneID drones, baselines an area and alerts on anomalies,
and meshes its findings to peers over LoRa.

This documentation set is split by audience.

## I want to run a Halberd

Operators deploying nodes, configuring scans, reading results.
**→ [User handbook](user/README.md)**

## I want to build on Halberd

Firmware contributors, protocol implementors, fleet integrators.
**→ [Technical handbook](tech/README.md)**

## I want to flash a node right now

The Halberd Web Flasher & Configurator runs in any Chrome/Edge
browser with Web Serial and walks you through firmware install +
initial configuration end-to-end.
**→ [Open the web flasher](index.html)**

## Other repo docs

- [`README.md`](./README.md). Repo overview, features, full command
 reference, API reference.
- [`CONTRIBUTING.md`](./CONTRIBUTING.md). PR workflow + hardware-
 testing conventions.
- [`SECURITY.md`](./SECURITY.md). Responsible-disclosure policy.
