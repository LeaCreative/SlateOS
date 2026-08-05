# Slate — handoff status

**Updated:** 5 August 2026, 00:30 (Indian/Mahe). Supersedes the 31 July note.
**Single point of truth for open work:** `docs/issue-prompts-open.md`.
`issues.md` is older and now partial — do not treat it as current.

---

## Where things stand

| | |
|---|---|
| **Firmware to flash** | `F03626A50F95` — InfiniTime-mirrored touch driver |
| **Companion installed** | `0.2.3-p13 (build 14)` |
| **Update path** | Slate→Slate SDP OTA over channel 5, proven repeatedly (I-13) |
| **Recovery** | Hold-to-blue → MCUBoot rollback. Works on confirmed images |

**Working:** boot, MTU 247 link, SDP session, local time (local wall-clock, not
UTC), battery, OTA with an on-watch progress bar, watch face with three
diagnostic lines, button navigation, return-to-face from a remote screen.

**Partly working:** P-1. A phone-composed display list renders and persists.
It lands **once per connection** — a repeat push does not arrive until the link
is dropped and re-established.

**Not working:** taps. The watch registers **zero** touch events (N-31).

---

## Next task (owner request)

**Mirror InfiniTime for battery, sleep/wake, input and vibration.** Spec,
file pairs and findings are in `docs/infinitime-parity.md` under "Scoped
mirroring task". The one already diagnosed:

**Vibration blocks the app task.** `main.cpp::do_haptic()` busy-waits up to
90 ms inline (DOUBLE = 25 + 40 + 25). InfiniTime's `MotorController` uses a
FreeRTOS one-shot timer and never blocks. Slate has no motor driver at all.
This directly worsens the app-loop stall behind the inbox drops, so it is both
a mirroring fix and a performance fix.

## The live blocker

**Display push now works.** JS sub-apps (Timer) and TestApp both open
reliably; frame drops are 0. Taps remain unproven — the touch driver has just
been rewritten to mirror InfiniTime (6-byte read from register 1, controller
config writes, edge-triggered IRQ) and is UNTESTED on hardware.

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
