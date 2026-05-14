# MAC randomization correlation

Modern phones (iOS, Android, Windows) rotate their MAC addresses
when probing to make tracking harder. Randomization correlation
tries to link those rotated MACs back to a single device using
behavioural signatures.

## How it works

A randomized MAC has the locally-administered (LAA) bit set in
the first octet (bit 1 of the first byte, e.g. `02:xx:`, `0A:`,
`12:`, etc.). Halberd flags these. But that alone tells you "this
is a random MAC", not "these N random MACs are the same device".

To make that link, the mode looks at signals that survive MAC
rotation:

- **IE-order signature**. The order in which a probe request
 lists its Information Elements is determined by the OS and
 Wi-Fi chipset firmware. Two probes from the same device have
 the same IE order. Two from different devices typically
 don't. This is the strongest signal.
- **Channel sequencing**. A device probing channels in the same
 sequence (e.g. 1 then 6 then 11) creates a temporal
 fingerprint.
- **Timing patterns**. The interval between probe bursts is
 characteristic per OS / driver.
- **RSSI patterns**. Two randomized MACs at very different RSSI
 during the same time window are unlikely to be the same
 physical device.
- **Sequence number correlation**. 802.11 sequence numbers
 follow predictable patterns per device.

The mode combines these into a confidence-based linking
decision. Above a threshold, two MACs get the same identity
ID (`T-XXXX`). Below, they stay separate.

## When to use it

- Long-running surveillance where MAC-only tracking fails.
- Counting unique devices in an area where many randomize.
- Forensics on probe captures where the MACs are obviously
 randomized (LAA bit set).

Not the right mode for:

- "Find this specific device right now". Use
 [target hunt](target-hunt.md). The randomization correlator
 produces statistical groupings, not real-time hits.
- "What's around me?". Use [device scan](device-scan.md).

## Starting a scan

```
@HB01 RANDOMIZATION_START:<mode>:<seconds>[:FOREVER]
```

| Field | Values |
|---|---|
| `mode` | `0` = Wi-Fi, `1` = BLE, `2` = both |
| `seconds` | Duration. `FOREVER` overrides to run until `STOP` |

Examples:

```
@HB01 RANDOMIZATION_START:0:1800        # 30 minutes Wi-Fi
@HB01 RANDOMIZATION_START:2:0:FOREVER   # continuous, both bands
```

Stop with `@HB01 STOP`.

## Output

An identity table where one entry can map to several MACs.
Visible on the web UI **Randomization** page and via the
`/randomization/identities` REST endpoint.

Each identity (`T-XXXX`) carries:

- Number of linked MACs (typical: 1 to 5 per device per
 session).
- IE-order signature (full and minimal forms).
- Linking confidence (percentage).
- First-seen / last-seen.
- Optional: a "stable" MAC if the device leaked its real
 hardware address once (global MAC leak).
- Wi-Fi to BLE cross-correlation when both radios see the same
 device.

## Limits

- Up to **30 simultaneous identities** in RAM.
- Up to **50 linked MACs per identity**.
- Identities persist to SD across reboots.

Use `POST /randomization/clear-old` periodically to prune stale
identities that haven't been seen in a while.

## Worked example

Long-running surveillance of a public space. After two hours of
captures, the raw probe DB shows 437 distinct MACs. That's
nowhere near the actual headcount. Run the correlator:

```
@HB01 RANDOMIZATION_START:0:7200        # 2 hours, retroactive on the recent capture
```

The identity table now shows perhaps 60 unique identities, each
mapping to between 1 and 12 historical MACs. That's much closer
to the real headcount. Subsequent visits by the same identity
get matched against the saved signatures and don't inflate the
count further.

## See also

- [Probe-request detection](probe-detect.md). The underlying
 capture this mode analyses.
- [Target hunt](target-hunt.md) for hunting a *known* device,
 vs. This mode's "count unknown devices" framing.
- [Data formats](./data-formats.md).
- Web UI: **Randomization Results** page.
