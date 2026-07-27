# Session + input (M7)

Normative behaviour: roadmap §4.4–§4.6.

## Ownership

| Concern | Owner |
|---|---|
| Profile **set** (names, backlight, connection interval) | **Firmware** (`session_profiles.hpp`) |
| Profile **selection** | Phone (`SET_PROFILE` by id only) |
| Scroll position / focus / within-screen back | **Watch** (zero RTT) |
| Semantic events (`TAP`, `BACK`, …) | Wire channel 2 |
| Display lists | Phone → channel 1 |

Adding a profile is a firmware change. Choosing among existing profiles is not.

## CONTROL channel (ch 0)

| Op | Dir | Role |
|---|---|---|
| `HELLO_OFFER` | watch→phone | fw/protocol version, 240×240, 32-byte opcode bitmap, free DL bytes, profile catalog, asset hashes (0 until M11), flags |
| `HELLO_ACCEPT` | phone→watch | profile id, 8-byte phone id, host semver |
| `HELLO_REJECT` | phone→watch | negotiation failure (stay Connected) |
| `HEARTBEAT` | phone→watch | every 2 s |
| `SET_PROFILE` / `PROFILE_ACK` | both | select / ack-or-nack |
| `SCREEN_PUSH` / `POP` / `REPLACE` | phone→watch | stack ops; DISPLAY applies next |
| `CREDIT` | watch→phone | free display-list bytes |
| `GOODBYE` | either | tear down remote stack |

Heartbeat: **2 misses → stale overlay**; **5 misses → pop remote stack**, `SESSION_END(TIMEOUT)`, watch face.

## States

`DISCONNECTED → CONNECTED → READY ⇄ ACTIVE ⇄ IDLE`

Reconnect: same phone id sets `same_phone`; different phone clears residual depth (already cleared on link-down).

## Host tests

```powershell
.\scripts\run_host_tests.cmd session
```

(Uses llvm-mingw on PATH / copies `libc++.dll` — see `docs/ble.md`.)

Covers: clean connect, negotiation failure, mid-session link loss, reconnect same/different phone, heartbeat stale/drop, profile invent-nack.
