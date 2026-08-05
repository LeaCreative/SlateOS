# SDP OTA verification (I-13)

Channel-5 OTA — chunked, resumable, SHA-256 verified — has never completed on
a durable, confirmed Slate install. This is the matrix that closes I-13, plus
what to have in place before starting so a failure is diagnosable on the first
attempt rather than the second.

Firmware: `include/ota_xfer.hpp`, `src/ota_xfer.cpp`, `src/ota_slot.cpp`.
Companion: `SlateOtaActivity` / `SlateOtaService` ("Update Slate firmware
(SDP OTA)" on the main screen), protocol in `sdp-core/slate/ota/OtaXfer.kt`.

## Preconditions

| Requirement | Why | Verified by |
|---|---|---|
| Battery ≥ 30 % and reading true | The gate is only meaningful once the ADC is right (N-12) | Overlay line 2 `adc/mV`; ~3.8 V ≈ 44 % |
| Link stable at ATT MTU 247 | A 23-byte MTU turns a ~120 KB image into an endurance test (N-15/N-16/N-17) | Companion shows `ATT MTU: 247` and `ready to push` |
| Running image **confirmed** | OTA refuses while the image is on trial — see below | No amber bar; companion shows "Confirmed — image is permanent" |
| One BLE central only | `SLATE_BLE_MAX_CONNECTIONS` is 1; nRF Connect will hold the slot | Companion contention warning (I-16) |

## Instrumentation to have running

- **Watch overlay, third line** — `<percent>/<last NAK>/<NAK count>`. It
  appears only once a transfer has begun or been refused. NAK reasons: 0 ok,
  1 busy, 2 bad message, 3 hash fail, 4 too large, 5 no storage, 6 low
  battery, 7 yield, **8 image unconfirmed**.
- **Watch RTT** — every refusal, and progress at each 10 %, with byte offsets.
- **Companion logcat** — per-chunk `OTA chunk off=… len=… acked=… credit=…`
  and every decoded watch message. Follow with:

  ```
  adb logcat -s SlateLink
  ```

The point is to be able to say *which offset* a stalled transfer died at and
what the watch thought of it, without reflashing to add logging.

## The IMAGE_OK interplay

**The guard did not exist before I-13 and has been added.** `ota_xfer` now
takes an `image_confirmed` hook, wired in `main.cpp` to
`!slate::boot::needs_confirm()`, and refuses `BEGIN` with
`NakReason::Unconfirmed` (8) while the running image is on trial.

The reasoning: MCUBoot reverts an unconfirmed image on the next reset. If a
second image were installed on top of one that is still on trial, a revert
would land on firmware nobody validated, and the known-good fallback — the
entire point of the confirm dwell — would be gone. So OTA waits for
`IMAGE_OK`.

The companion surfaces this as *"Watch image is still on trial — keep the
watch connected until the amber bar clears, then retry"* rather than a bare
protocol error.

## Run 1 — 1 August 2026 (row 1 failed; N-19)

The first attempt stalled at **512 / 133236 B**. Root cause was a credit
desynchronisation, not the transfer itself: the link inbox holds one message,
so of the four chunks the companion sent against its 2048-byte window, three
were dropped while the app task processed the first. The companion had spent
its credit locally; the watch had only spent 512 and re-advertised nothing.
Both waited. The watch overlay also showed the `BEGIN` slot erase blocking
the drain for ~5.3 s, which guarantees a large drop window at the start.

## Run 2 — 2 August 2026 (row 1 failed again; N-19 continued)

With the companion's resync fix but the watch still on the pre-fix image, the
transfer reached **10752/133252 B** before exhausting 20 resyncs. The numbers
name the mechanism: 10752 = 21 × 512, one chunk recovered per resync, so
**exactly one chunk of every four-chunk burst was landing**. That is what a
2048-byte window into a one-message inbox produces.

So the recovery path was never the real fix — the window was. The watch's OTA
window is now **one chunk** (512 B), matching what the link can actually hold,
with credit restored in full on every ACK: send one chunk, receive ACK +
credit, send the next. Re-run the matrix from row 1 with `47A3FA74D258`, and
note the watch must be on that image — a companion-only update cannot fix
this, because the window is advertised by the watch.

## Test matrix

Record the result, the overlay's third line, and the last companion log line
for each row. Any failure becomes a new issue in `issues.md`.

| # | Case | How to run it | Expected |
|---|---|---|---|
| 1 | Happy path | Confirmed image, battery > 30 %, push a full image | Transfer → COMMIT → MCUBoot swap → amber bar → hold a central 10 s → `IMAGE_OK`. Overlay ends at `100/0/0` |
| 2 | Resume at 25 % | Walk out of range (or disable BT) at ~25 %, return | Companion re-sends BEGIN with the same id; watch ACKs its offset; transfer continues from there, not from 0 |
| 3 | Resume at 50 % | As above at ~50 % | Same |
| 4 | Resume at 75 % | As above at ~75 % | Same |
| 5 | Resume after app kill | Force-stop the companion mid-transfer, reopen, restart the same image | Same id → resume from the watch's offset. A *different* image must not resume onto a partial slot |
| 6 | Battery gate | Discharge below 30 %, unplugged, start a transfer | Refused at BEGIN with NAK 6; overlay `0/6/1`; companion says battery too low |
| 7 | Unknown battery | Force `kPercentUnknown` (0xFF) | Refused with NAK 6 — unknown must **not** pass as "≥ 30" |
| 8 | Unconfirmed image | Flash any image, do **not** let it confirm (no 10 s dwell), then start OTA | Refused at BEGIN with NAK 8; overlay `0/8/1`; companion explains the trial state |
| 9 | SHA mismatch | Corrupt a byte in the image before sending | All chunks accepted, COMMIT rejected with NAK 3, **no reboot**, slot not activated |
| 10 | Oversized image | Send an image larger than the secondary slot | Refused with NAK 4 at BEGIN |
| 11 | Contention | Connect nRF Connect, then start OTA | Companion names the blocker rather than timing out silently (I-16) |

Rows 2–5, 9 and 10 are also covered by `tests/host/test_ota_xfer.cpp` against
a simulated link, so the on-watch run is testing radio and flash rather than
protocol logic. Row 8's guard has a host test too.

## If throughput disappoints

PHY is 1M and DLE is unnegotiated: the connect path deliberately does not
drive them (N-15 — issuing MTU/DLE/PHY/param updates inside the GAP connect
callback collapsed the link). `ble::negotiate_now()` exists for exactly this
workload. Call it **from the app task, after discovery has settled** — never
from a GAP callback.

The other known cost is the local repaint at ~199 ms (N-18, down from
611 ms). OTA chunks drain on the app task, so a repaint delays them; credits
should throttle rather than fail. If a stall lines up with a repaint, that is
the cause, and the next lever is skipping the SPI push for unchanged tiles.

## Recovery

A failed OTA leaves the current image running — the secondary slot is only
activated at COMMIT, after the hash matches. If a swapped image misbehaves,
hold the button through the pinecone until the logo fills **blue**: MCUBoot
rolls back to the previous image even when the current one was validated.
See `docs/recovery-sealed.md`.
