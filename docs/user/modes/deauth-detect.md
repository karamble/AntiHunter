# Deauth attack detection

Spot active 802.11 deauthentication and disassociation floods. A
common Wi-Fi denial-of-service technique often used to force
clients to disconnect from APs so they re-associate (revealing
themselves to the attacker, or attaching to a rogue AP).

## When to use it

- Detecting active Wi-Fi attacks on your network or in your
 vicinity.
- Security monitoring of an event venue or sensitive site.
- Pairing with [baseline](baseline.md). A deauth flood usually
 shows up there as sudden client-population churn.

## What's captured

802.11 management frames of two kinds:

- **Deauthentication** (frame subtype 12). Sent by an AP or a
 client to terminate authentication.
- **Disassociation** (frame subtype 10). Sent by an AP to
 detach a client from its BSS.

Both are unencrypted and trivially spoofable in the legacy
802.11 standard. Modern WPA3 networks use Protected Management
Frames (PMF) which sign deauths cryptographically. PMF-protected
deauths still appear in the capture but can be verified
out-of-band.

## Starting a scan

```
@HB01 DEAUTH_START:<seconds>[:FOREVER]
```

Examples:

```
@ALL DEAUTH_START:300                   # 5 minutes
@HB01 DEAUTH_START:0:FOREVER            # continuous monitoring
```

Stop with `@HB01 STOP`.

## Output

Per-frame alert on the mesh:

```
HB-A1B2: ATTACK: DEAUTH SRC:11:22:33:44:55:66 DST:AA:BB:CC:DD:EE:FF RSSI:-42 CH:6
```

And a JSONL record in `/deauth.jsonl`:

```json
{"t":1747237900,"src":"11:22:33:44:55:66","dst":"AA:BB:CC:DD:EE:FF","bssid":"22:33:44:55:66:77","reason":7,"rssi":-42,"ch":6,"type":"deauth"}
```

Fields:

- `src` is the spoofed source MAC (usually the AP being spoofed).
- `dst` is the target client (or `FF:FF:FF:FF:FF:FF` for
 broadcast attacks).
- `bssid` is the AP's BSSID in the frame header.
- `reason` is the 802.11 reason code (7 = class 3 frame received
 from nonassociated STA, common in attacks).
- `rssi` and `ch` describe the capture environment.
- `type` is `deauth` or `disassoc`.

## Telling attacks from legitimate traffic

Legitimate deauths happen all the time:

- A client roams between APs in a managed Wi-Fi network.
- A client disconnects voluntarily (sleep, airplane mode).
- An AP kicks an idle client.

Attack signatures:

- **Broadcast deauths from one source repeated rapidly**. A
 client doesn't ask "everyone disconnect from this AP". An
 attacker does.
- **High per-second rate from a single source**. Hundreds of
 deauths in a few seconds is not normal traffic.
- **Mismatched OUI**. The deauth claims to come from a specific
 AP MAC, but the RSSI/channel pattern doesn't match where that
 AP normally lives.
- **No PMF** but the AP claims to be a WPA3 network with PMF
 required. The deauth is forged.

The mode doesn't auto-classify. It logs everything and lets the
operator (or diginode-cc post-processing) decide.

## Channel-hopping

Deauths are channel-specific. The mode walks the configured
Wi-Fi channel list at a roughly 100 ms per channel cadence.
During a known attack you'll see frames concentrated on the
target AP's channel.

For continuous protection of a specific network, set
`CONFIG_CHANNELS` to just that AP's channel.

## Rate limits during floods

A real attack can produce hundreds of deauths per second. The
node aggregates rather than forwarding each frame individually
to the mesh. Aggregation reduces mesh saturation but still
captures every frame in `/deauth.jsonl` for postmortem.

## Worked example

You suspect someone is deauth'ing the guest network at an
event. Confirm:

```
@ALL DEAUTH_START:300:FOREVER
```

Bench-test the alarm path first with `aireplay-ng` from another
device:

```bash
aireplay-ng --deauth 100 -a AA:BB:CC:DD:EE:FF wlan0mon
```

The Halberd should report:

```
HB-A1B2: ATTACK: DEAUTH SRC:AA:BB:CC:DD:EE:FF DST:FF:FF:FF:FF:FF:FF RSSI:-35 CH:6
HB-A1B2: ATTACK: DEAUTH SRC:AA:BB:CC:DD:EE:FF DST:FF:FF:FF:FF:FF:FF RSSI:-35 CH:6
... (one frame per aireplay deauth, rate-limited to the mesh)
```

If you see these in a real deployment without running the test,
you have an actual attacker.

## See also

- [Mesh command reference](./commands.md).
- [Data formats](./data-formats.md). `/deauth.jsonl` schema.
- [Baseline](baseline.md). Anomalies caused by a deauth flood.
