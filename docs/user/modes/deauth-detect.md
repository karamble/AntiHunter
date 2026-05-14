# Deauth attack detection

Spot active 802.11 deauthentication and disassociation floods — a
common Wi-Fi denial-of-service technique often used to force clients
to disconnect from APs so they re-associate (revealing themselves to
the attacker, or attaching to a rogue AP).

> 🚧 **Stub page.** Content lands in a follow-up commit.

## When to use it

- Detecting active Wi-Fi attacks on your network or in your area.
- Security monitoring of an event venue / sensitive site.
- Pairing with [Baseline](baseline.md) — a deauth flood typically
  shows up as sudden client-population churn there too.

## What this page will cover

- `DEAUTH_START:<seconds>[:FOREVER]` syntax.
- What's captured: source MAC, destination MAC (target client or
  broadcast), BSSID (the AP being spoofed), reason code, RSSI,
  channel.
- Distinguishing legitimate deauths (clients voluntarily leaving an
  AP) from attack patterns (broadcast deauths from a single source,
  rapid repetition, mismatched OUI).
- Output → `/deauth.jsonl` and mesh alerts.
- Channel-hopping strategy during a deauth-only scan.
- Rate limits and what happens during a flood (the node doesn't
  forward every single frame to the mesh; aggregates).

## Worked example

A bench test with an `aireplay-ng` deauth attack against a known AP,
and what the resulting `/deauth.jsonl` entries look like.

## See also

- [Mesh command reference](../commands.md) — full `DEAUTH_*` syntax.
- [Data formats](../data-formats.md) — the `/deauth.jsonl` schema.
