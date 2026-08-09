# Slate — handoff status

> ## ⚠ STALE — frozen at 6 August 2026, 23:30
>
> **Everything below this box describes the state on 6 August and has not been
> maintained since.** The build ids, the "working / partly working" lists and the
> open items are all out of date by three days of work — including the settings
> screen, the sync protocol, and two accelerometer fixes.
>
> **Do not act on anything in this file without checking it first against:**
>
> - `docs/handover-prompt.md` — the current brief: build state, traps, how to work here.
> - `docs/issue-prompts-open.md` — the single point of truth for open work. Newest section is at the top.
>
> It is kept because the 6 August write-ups below (the launcher, the 5x7 font,
> the tap path) are still the best account of how those parts came to be.
> `issues.md` is older still and only partial.

---

## Where things stand *(as of 6 August — superseded)*

| | |
|---|---|
| **Firmware to flash** | `AB044776E1FE` — circle span fill + watchdog pets in the tile loop (a sub-app could reset the watch) |
| **Last flashed** | `609EF4F6979A` (stamp 10:43) — scroll fix, launcher, 5x7 font |
| **Companion installed** | `0.6.1-p24 (build 25)` — launcher, sub-app settings, credit-leak fix |
| **Update path** | Slate→Slate SDP OTA over channel 5, proven repeatedly (I-13) |
| **Recovery** | Hold-to-blue → MCUBoot rollback. Works on confirmed images |

**Working:** boot, MTU 247 link, SDP session, local time (local wall-clock, not
UTC), battery, OTA with an on-watch progress bar, watch face with three
diagnostic lines, button navigation, return-to-face from a remote screen.

**Partly working:** P-1. A phone-composed display list renders and persists.
It lands **once per connection** — a repeat push does not arrive until the link
is dropped and re-established.

**Newly working (5 Aug, 21:29):** taps, and the whole I2C bus with them.
P-1 is closed end to end against the `timer` JS sub-app — see below.

**Still open:** the app-task stall (887 ms) and inbox drops (16), which the
haptic fix did **not** move. Cause is the ~222 ms face repaint, not vibration.

---

## App launcher + 5x7 font (6 Aug) — built, untested on hardware

**Font 1 is a second built-in, compiled into flash — not an asset.** The 3x5
had no letters at all (codepoints 45-58: digits and three punctuation marks),
which is why every version line has read as boxes. Both fonts now come from
ASCII-art sources under `tools/codegen/` through one generator, so the
firmware header and the Kotlin binding cannot drift.

The asset-pack route was measured and rejected: `GlyphCache` carries a 6 KB
blob and contributes **0 bytes to .bss today** — it has never been
instantiated — against 448 B of RAM headroom, and the asset-transfer path has
never run on hardware. Compiled-in costs 665 B of flash and no RAM.

| Piece | Where |
|---|---|
| 5x7 font, 95 glyphs + tick/cross/smiley/heart | `tools/codegen/font1_art.py` |
| Launcher (3 rows/screen, 72 px = 3 scroll steps) | `companion/.../apps/LauncherApp.kt` |
| Swipe-left → launcher, tap → focus | `CompositorHost.onInputMessage` |
| Buzz Phone JS sub-app | `companion/examples/vibrate/` |
| `slate.phone.vibrate()` + `ScriptPermission.Vibrate` | `shared-js/slate_host.js`, `BindingSurface.kt`, `CompositorHost.handlePhoneAdapter` |
| "Not connected" screen | `local_ui.cpp::build_disconnected` |

**Trap hit and cleared:** `generated/SdpWire.kt` is labelled generated but had
been **hand-edited** — `TIME_SYNC`, `CONFIRM_STATUS_REQUEST` and
`CONFIRM_STATUS` existed only in the Kotlin, never in `shared/sdp_wire.json`.
Running the generator deleted them and the companion stopped compiling. They
are now in the JSON where they belong. Assume other generated files may carry
the same debt.

`tools/codegen/generate.py` now fails loudly if `max_font_id` disagrees across
`shared/fonts/`, `sdp_wire.json` and `sdp_opcodes.hpp` — the parser rejects
text ops above `kMaxFontId`, so a half-added font would silently stop
rendering text rather than erroring.

## N-31 closed — taps work (5 Aug, 21:29, build `16D04B4EF949`)

Diag line 3 read `0.16/31.0.0.3/244.0.8.6`: **244 touch interrupts, 0 read
failures**, 8 touch events decoded, 6 of them landing on a hit rect. Every
previous build failed one read per interrupt, without exception.

Confirmed independently in the companion log: three `notify len=7` events,
distinct from the steady `len=5` CREDITs, each followed ~30 ms later by a
63 B display-list push — taps arriving at the phone and the sub-app pushing
new state back. The Timer counted down and paused on tap.

**P-1 is closed**, and closed the way `docs/issue-prompts-open.md` required:
against a **JS sub-app**, not TestApp. Display list composed on the phone →
rendered on the watch → tap → input event to the phone → new list. `dl_ok=31`,
frame drops 0.

Also confirmed by this build, having never been observed working before:

- **Battery parity.** 4190 mV / **99 %** with the charger connected. The 99 is
  the InfiniTime charging clamp doing its job (power present, still charging →
  not `isFull`). The reading rose ~94 mV against the same cell state on the
  previous build, which is what the 10 µs → 40 µs acquisition fix predicted.
- **The BMA is reachable** for the first time — it sits on the bus that has
  never worked. Steps still read 0, but the watch was stationary; that number
  is unverified either way.

## What the two failed touch tests measured

`F03626A50F95` (line 3 `9.9.0.0`) and `F4A879DE6880` (`5.5.0.0`): in both,
**every touch interrupt arrives and every I2C read behind it fails.** The IRQ
half of N-31 is fixed — one interrupt per tap, no storm.

The read half was **the TWIM SHORTS bit positions in `nrf52832_regs.hpp`**,
which were shifted against the nRF52832 PS and against Nordic's own
`nrf52_bitfields.h` vendored in `third_party/`. `twi::write` suspended instead
of stopping; `twi::write_read` never issued the repeated START. Neither ever
raised `EVENTS_STOPPED`, so both timed out — always, on every device. That is
why **no I2C transfer has ever succeeded on this watch**: touch, the BMA
(steps have read 0 throughout) and the HRS3300 sleep write (the heart-rate
sensor has been powered all along). Fixed in `16D04B4EF949`.

An earlier theory — SCL/SDA drive `S0S1` where InfiniTime uses open-drain
`S0D1` — was a real divergence and is fixed, but it was **not** the cause:
`F4A879DE6880` contained that fix and changed nothing.

The instrument that was missing: the failure counter said only *that* a read
failed, never *how*. Line 3's touch group now carries the `twi::Status` of the
last read as a fifth field.

Line 1 also read `stall = 878 ms` and line 2 `render = 218 ms`.

## Next task (owner request) — **done, awaiting hardware**

**Mirror InfiniTime for battery, sleep/wake, input and vibration.** All four
are implemented and carried into `16D04B4EF949`; the write-up, including what
each change should move on the overlay, is in `docs/infinitime-parity.md`
under "Scoped mirroring task". Headlines: vibration is now a FreeRTOS one-shot
timer (`src/motor.cpp`) instead of a 25–200 ms busy-wait on the app task; TWI
transfers wake/sleep per transaction as InfiniTime's `TwiMaster` does; the
battery power-present pin (P0.19) is read for the first time and the SAADC
acquisition time is 40 µs rather than 10 µs; touch samples are validated
rather than clamped, one gesture per touch. Only the vibration and TWI parts
have been on hardware at all, and none of it has been observed working —
`F4A879DE6880` could not read a single sensor.

## The live blocker

**Display push now works.** JS sub-apps (Timer) and TestApp both open
reliably; frame drops are 0. Taps are still unproven on hardware.

Historic note: diag line 3 read `538.82` — 538 frames rejected by the reassembler, 82 dropped
by the inbox, with the watch sending almost nothing back (one CREDIT in 30 s of
captured log). Everything else is downstream of that.

`src/sdp_frame.cpp` — the reassembler — has **not been examined** in this
session. Start there, not in the compositor and not in the session.

Cheap next experiment, no flash required: set `ClockApp` to
`RefreshPolicy.Manual`. The ambient app re-renders while buried at a 1/min
quota and overwrites the focused app (N-34); this isolates that from the frame
drops.

---

## Practices to keep (learned the hard way)

1. **The operator's observation beats the agent's inference.** When they say
   something is not on screen, that is the fact. Four separate bugs were called
   correctly by the user and argued against first.
2. **Verify rendering before claiming it works.** `adb shell uiautomator dump`
   gives element bounds for the phone. For the watch, ask — the agent cannot
   see it. "Present in the view hierarchy" is not "visible": companion buttons
   sat under the system bars for four builds (`targetSdk 35` forces
   edge-to-edge).
3. **Bump `versionCode` on every installed build.** It sat at 1 for roughly
   fifteen installs, defeating the entire purpose of the version display.
4. **Guard at the choke point, not per call site.** `Core::show_current()` has
   twelve internal callers; gating them one at a time failed three times.
5. **Instrument before theorising.** Every counter added to diag line 3
   immediately answered a question that had already cost several
   build-and-flash cycles.
6. **State every change.** Each build handed over gets a "what changed" list,
   including incidental edits.

---

## Repeated failure mode to avoid

Several fixes were applied on unverified theories and had to be reverted or
superseded: a diag-interval change based on a wrong flicker diagnosis;
"display sleep" and "white panel with residual pixels" offered as explanations
for a screen that was neither; the ClockApp removal blamed for a regression it
did not cause. In each case one measurement would have settled it in seconds.
Measure first — flashing costs the operator real time.
