# Security and tamper response

Halberd is built to fail safely when it's been tampered with or
when its location is compromised. This page covers the tamper
detection chain, the auto-erase policy, and the remote
secure-erase token mechanism.

## What's protected vs not

Halberd's security model defends against:

- A node being moved from its deployed location.
- A node being opened or shaken before secure storage of its
 data has been confirmed.
- An attacker on the same Wi-Fi AP triggering destructive
 commands accidentally (the `ERASE_REQUEST` / `ERASE_FORCE`
 challenge-response is the gate).

It explicitly does **not** defend against:

- A determined attacker with physical access and time. JTAG is
 exposed on the XIAO. A cold-boot attack on RAM is feasible.
- Anyone who already knows the secure-erase token. Treat it like
 a password.
- An attacker pulling the SD card before the auto-erase fires.

## The tamper sensor

The carrier mounts an SW-420 vibration sensor (`U2`) on a digital
input. The sensor is a bead-and-spring contact-closure device, so
it triggers on board motion, drops, or chassis vibration.

What it detects:

- The node being moved or carried.
- Someone opening the enclosure if the SW-420 is mounted on the
 lid.
- Power-tool or knock-down impacts during forced entry.

What it doesn't detect:

- Slow tilt (the spring doesn't break contact slowly enough).
- Thermal stress.
- A static observer.

Control:

```
@HB01 VIBRATION_ON
@HB01 VIBRATION_OFF
@HB01 VIBRATION_STATUS
```

Default: on. Each detected vibration is logged to
`/vibrations.jsonl` and (when active) feeds the auto-erase policy.

## Auto-erase policy

The "if tampered, wipe" rule. **Disabled by default**, must be
turned on explicitly.

```
@HB01 AUTOERASE_ENABLE:setup:erase:vibs:window:cooldown
```

Parameters (all seconds except `vibs`):

| Parameter | Meaning | Typical value |
|---|---|---|
| `setup` | Grace period after enable before triggers fire | 60 |
| `erase` | Countdown after tamper detection before the wipe | 30 |
| `vibs` | Vibration count threshold within `window` | 3 |
| `window` | Rolling window for counting vibrations | 30 |
| `cooldown` | Minimum time between tamper triggers | 300 |

So `AUTOERASE_ENABLE:60:30:3:30:300` means: wait 60 s after
arming, then fire if 3+ vibrations occur within any 30 s rolling
window, count down 30 s before wipe, then ignore further triggers
for 5 minutes.

Status:

```
@HB01 AUTOERASE_STATUS
@HB01 AUTOERASE_DISABLE
```

When the policy fires, the node broadcasts:

```
HB-A1B2: TAMPER_DETECTED: Auto-erase in 30s [GPS:48.137,11.576]
```

The 30 s countdown gives an operator the chance to cancel with
`AUTOERASE_DISABLE` if it was a false positive.

## What gets erased

The wipe (whether triggered by auto-erase or by `ERASE_FORCE`):

- **NVS**: targets, AP credentials, node ID, scan defaults, RF
 preset, baseline statistics, audit log.
- **SD card data partitions**: `/probes.jsonl`, `/deauth.jsonl`,
 `/drones.jsonl`, `/vibrations.jsonl`, `/halberd.log`,
 `/probedb.jsonl`. Multi-pass overwrite, then FAT delete.

What survives:

- The firmware itself (factory partition). The node is still
 functional, just blank.
- The RTC time (DS3231 has its own coin cell).
- The GPS module (the C5 keeps its almanac data internally).

After a wipe the node is equivalent to a freshly flashed,
unconfigured device. Use the web flasher or `flashHalberd.sh -c`
to reconfigure it.

## Remote secure erase

The challenge-response flow used to wipe a node on demand. Useful
when a deployment is being recovered or when a node is suspected
to be compromised.

1. Send `ERASE_REQUEST` to the target node:

   ```
   @HB02 ERASE_REQUEST
   ```

2. The node generates a one-time token tied to its current state
 and emits it on the mesh:

   ```
   HB-02: ERASE_TOKEN: HB_12345678_87654321_00001234 (expires in 60s)
   ```

3. To confirm, send `ERASE_FORCE` with the exact token:

   ```
   @HB02 ERASE_FORCE:HB_12345678_87654321_00001234
   ```

4. The node wipes immediately. No countdown.

The token expires after about a minute and is single-use. Without
it, no remote actor can wipe a node by accident. To cancel a
pending request:

```
@HB02 ERASE_CANCEL
```

## Testing tamper response without losing data

You'll want to validate the tamper chain on the bench before
deploying.

- Use a node with no real data on its SD card.
- Use `AUTOERASE_ENABLE` with a short countdown (e.g.
 `:60:5:3:30:60`).
- Shake the carrier to trigger. Watch the mesh for the
 `TAMPER_DETECTED` frame.
- Cancel with `AUTOERASE_DISABLE` before the 5 s timer expires
 to verify the cancellation path.
- Repeat without cancelling to verify the wipe completes.

The web UI's **Security** page shows live SW-420 trigger counts,
the auto-erase setup countdown, and any pending wipe state.

## Privacy mode

Distinct from secure erase. Privacy mode redacts MAC last octets
and SSIDs in result tables before they're shown on screen. Useful
when you need to share a screenshot without leaking the data.

Privacy mode toggles on the web UI Privacy button. SSIDs are
hashed as `net#XXXX` so you can still see correlation patterns
without exposing the network names.

## See also

- [Mesh command reference](commands.md). All security command
 syntax.
- [Data formats](data-formats.md). What's in
 `/vibrations.jsonl`.
- [`SECURITY.md`](././SECURITY.md). Responsible-disclosure
 policy for the project itself.
