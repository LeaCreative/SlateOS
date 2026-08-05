# EvoTime — open work register

> **This file is the single point of truth.** `issues.md` is older and now
> partial; where they disagree, this file wins. Updated 5 August 2026.

---

## Current state (5 Aug 2026, 00:30)

**Firmware on the watch:** `EB90B288AE1F`. **Companion:** `0.2.1-p11 (build 12)`.

**P-1's display half is PROVEN (5 Aug, 13:22).** The `timer` **JS sub-app**
renders on the watch reliably — three presses, three pushes, three CREDITs,
`dl_ok = 3`, zero frame drops, four inbox drops (all from connection setup).

What made it reliable, in order of contribution:

| Fix | Effect |
|---|---|
| Periodic DIAG ping disabled | Frame drops 538 → **0**. The watch rejects channel 7 unless built `SLATE_BLE_DIAG=1`; the ping ran every 5 s and every rejection was counted as a drop |
| `textScaled` added to the **JS** builder | JS sub-apps were structurally incapable of legible text — `b.text()` only emits the 3×5 base font |
| Duplicate-push coalescing | Focus emitted the same list 3× in ~33 ms (`onFocus`, `onRender`, scheduler flush) |
| Inter-message gap 30 ms → **250 ms** | The real one. The pre-display CONTROL and the list are a back-to-back pair; 30 ms cleared the nominal 20 ms drain but not a 230 ms repaint, so the CONTROL held the single-slot inbox and the **list** was discarded |
| Credit gate on display pushes | Correct in principle, but it never engages in practice — presses are further apart than its 1.5 s timeout |

**Still open:** taps (`touch.hit` = `0.0`, N-31 — the `buses_wake()` fix did
**not** work, so the IRQ is not reaching the driver at all), and a long-run
degradation where the watch stops replying entirely until reconnect (see
below).

### The sub-app stack — what actually exists

| App | Kind | Priority | Notes |
|---|---|---|---|
| `ClockApp` | Kotlin | **AMBIENT** | Ambient watch face. Owner wants it gone; see below |
| `NotificationsApp` | Kotlin | NORMAL, raised at **INTERRUPT** | Raised by an incoming notification (`maybeInterrupt`), so it can pre-empt any screen |
| `TestApp` | Kotlin | NORMAL | The P-1 reference app |
| `timer`, `camera`, `navigation` | **JS sub-apps** | NORMAL | In `companion/examples/`. The Timer is *not* the clock app — separate things |

**How AMBIENT differs** (`Compositor.kt`):
- A new AMBIENT focus **replaces** the existing ambient base rather than
  stacking (`stack.removeAll { AMBIENT }; stack.add(0, …)`).
- **The ambient app keeps re-rendering while buried**, at a 1/min quota
  (`tick()`: "Ambient may still update when buried"). *This is why the clock
  overwrites TestApp seconds after it appears.*
- Unhandled input falls back to the ambient entry (`dispatchInput`).

**Correction to an earlier claim:** I said TestApp "needs" an AMBIENT stack
base. **The code does not support that.** `requestFocus` appends a NORMAL entry
regardless of whether an ambient base exists. Restoring `ensureAmbient()`
coincided with pushes landing again, but no mechanism has been identified —
treat that correlation as unexplained, not as a cause.

### Open defects, newest first

| ID | Summary | Status |
|---|---|---|
| **N-34** | Ambient `ClockApp` re-renders while buried and overwrites any focused app | Open. Fix: `RefreshPolicy.Manual`, or remove the app and keep a stack base another way |
| **N-33** | `transfer_active()` flickers between chunks, so the face and OTA banner alternated | **Fixed** (3 s latch) — confirmed on hardware |
| **N-32** | `SdpWriteQueue` sequences never reset across reconnects | Fixed, but **did not** stop the frame drops — cause still open |
| **N-31** | Touch dead outside `State::Active` — `buses_idle()` disables TWIM1, which the CST816S needs | Open. Mitigated only |
| **N-30** | Session stale path discarded the remote screen | Fixed |
| **N-29** | Local face repainted over phone-pushed screens | Fixed — single guard inside `Core::show_current()` |
| **N-28** | CONTROL + DISPLAY written back-to-back; inbox dropped the second | Fixed (30 ms pacing + stall watchdog) |
| **I-17** | Display sleep is implemented via the ambient profile but **broken** | Open — corrected: it is not "missing" |

### P-1 acceptance must be a JS sub-app, not TestApp

TestApp is Kotlin. It was used to prove the transport with the V8 engine,
sandbox and IPC removed as variables — reasonable for bring-up, but it does
**not** test the product thesis, which is *JS sub-apps* telling the watch what
to draw and bridging to other phone apps.

The wire format is not the gap (`JsRuntimeTest` proves the Kotlin DSL and the
JS builder emit byte-identical lists); the untested parts are the JS engine,
the sandbox bindings and the host IPC on real hardware.

**Once the frame drops are fixed, close P-1 against the `timer` JS sub-app**
— `MainActivity` already has an "Open Timer (JS sub-app)" button and
`companion/examples/timer` already exists. Keep TestApp only as a transport
probe. The bridge path (notifications, media, navigation reaching a sub-app)
is separately unproven on hardware and needs its own acceptance test.

### N-35 — The watch stops replying entirely until reconnect

- **Status:** Open. **This is now the most important open defect.**
- **Evidence (5 Aug, 13:15–13:17):** for nearly two minutes the companion
  wrote a heartbeat every 2 s and received **no `notify` of any kind** — no
  CREDIT, no heartbeat reply, nothing. A reconnect at 13:17:19 restored normal
  traffic immediately.
- **Why it matters:** it explains "it works after several attempts" — the
  reconnect is doing the work, not the retries. All pacing and flow-control
  fixes are secondary; they only matter while the watch is answering at all.
- **Correlates with:** long uptime (16342 s when observed) and `stall = 929`
  ms on diag line 1. `inbox_drop` climbs continuously in this state while
  `dl_ok` stays flat — consistent with the app task being wedged rather than
  merely slow.
- **First measurement to take, before any code change:** does `paints` (diag
  line 1, field 3) keep climbing while the watch is unresponsive?
  - **Climbing** → the app loop is alive; the fault is in the link/inbox path.
  - **Frozen** → the app task is stuck, and everything else follows from that.

  That single reading splits the problem in two.

### The live blocker

**Frame drops.** Diag line 3 read `538.82` — 538 frames rejected by the
reassembler and 82 dropped by the inbox, with the watch sending almost nothing
back (one CREDIT in 30 s of log). This is why a second push does not land until
reconnect. `sdp_frame.cpp`'s reassembler has not been examined once; that is
where to start.

**Next experiment (cheap, no flash):** set `ClockApp` to
`RefreshPolicy.Manual`. That isolates whether the once-per-connection
behaviour is the clock overwriting the screen or the frame drops.

### Diagnostic overlay (firmware `EF53E080F129`)

```
line 1: reset/uptime/paints/button/stall/ble.rc/tickCatchup
line 2: phase.ms/mV/parse.render
line 3: frameDrop.inboxDrop/applied.rejected.dropped.sessState/touch.hit
```

Line 3 reads left to right along the message path — reassembly, inbox, session,
interpreter. **The first non-zero drop counter is where the message died.**
Reset reason is raw `RESETREAS` bits: `1` pin, `2` watchdog, `4` soft reset
(normal after DFU), `8` lockup.

---


Consolidated register of everything still open, after the 3 August session
closed out bring-up: the watch now tells the time and updates itself over the
air.

| | |
|---|---|
| **Repository** | `C:\Users\highj\Documents\Projects\EvoTime` |
| **Revision** | Rev 3, 3 August 2026. Replaces the `.docx` revisions, which are superseded and should not be used. |
| **Running image** | `CFEDE90DE7F4` (`0.1.0-m16`), installed by Slate's own SDP OTA, IMAGE_OK confirmed |
| **Scope** | 8 prompts. I-13 and I-6 are closed; the display-list push is new and is the recommended next work. |
| **Live status** | `issues.md` is the authoritative register. Where the two disagree, `issues.md` wins. |
| **Operator manual** | `docs/operator-manual.md` — every element on the watch, and how to navigate |
| **Companion manual** | `docs/companion-manual.md` — every button in the phone app, and the two notification paths |

---

## 1. What changed since the last revision

The previous revision described a watch that held a BLE link but had never
negotiated an SDP session. That is fixed, along with the chain of defects
behind it. Bring-up is over.

**The session had never negotiated.** HELLO_OFFER is a notification, and it was
sent on the GAP connect event — about 1.3 s before the central writes the TX
CCCD. NimBLE drops notifications to an unsubscribed client, so the offer went
nowhere, every time, on every build ever flashed (N-23). The session now starts
on `BLE_GAP_EVENT_SUBSCRIBE`.

**The companion was connecting twice.** `connect()` began with `close()`, and
CDM's presence callback fired ~250 ms after our own connect, tearing down a
GATT that had `requestMtu()` outstanding. Its `onMtuChanged` never arrived, and
since discovery is chained off that callback, services were never discovered
and every send failed "not ready" (N-24).

**Channel-5 SDP OTA is proven (I-13).** Two consecutive Slate→Slate updates
with no InfiniTime round-trip: 134180 B at ~46 kB/s, every 512 B chunk ACKed
and credited, zero NAKs and zero resyncs, then reboot, swap and IMAGE_OK after
the connected dwell. This is now the primary update path.

**The clock works.** Two defects: back-to-back CONTROL messages were dropped by
the single-slot inbox so TIME_SYNC never arrived (N-25); and once it did, the
companion was sending a UTC epoch to a watch with no timezone database (N-26).
It now sends local wall-clock, as CTS does.

**The watch is self-describing.** It shows its version and build stamp, and
live `UPDATING nn%` progress during a transfer. Several defects this session
were mis-attributed because the only way to tell one image from another was to
infer it from behaviour.

**The companion has an in-app log** with copy and share, plus its version in
the UI. Both of the last two root causes were found in that log.

---

## 2. How to use this document

Each prompt is self-contained — paste it verbatim. Prompts assume the standing
rules in `CLAUDE.md`, in particular that low-level behaviour mirrors InfiniTime
and that SDP wire constants live only in `include/sdp_opcodes.hpp`.

Prompts are ordered by recommended sequence, not by id.

> **Before starting any prompt: read `issues.md` first.** It is updated
> continuously and this document is not. Eight of the defects fixed in the last
> two days were not in any prompt document at the time they were found.

---

## 3. Where the hardware stands

The sealed watch boots reliably, holds a link at MTU 247, negotiates an SDP
session, keeps time, reports true battery voltage, and updates itself over the
air. Hold-to-blue rollback works even on a confirmed image and remains the
guaranteed recovery path.

- **Verified on hardware:** I-1, I-2, I-4, I-5, I-6, I-13, I-16, N-1 … N-26.
- **Not yet exercised at all:** the remote UI path. Nothing has pushed a
  display list from the phone and seen it render. That is the product thesis
  and it is entirely unproven — hence prompt P-1.
- **Thin margin:** RAM at 60968 B of 64 KB (93%), ~490 B below `__StackLimit`.
  Any static growth needs a map check (I-14).

---

## 4. Overlap map — what must not be worked twice

- **I-12 (CTS) is effectively moot.** TIME_SYNC on CONTROL carries local
  wall-clock and syncs at 300 ms / 2 s / 8 s after Ready, then every 15 min. A
  CTS client would duplicate a working path.
- **I-7 and I-8 are demoted, not closed.** Both concern Nordic legacy DFU,
  no longer the primary update path. They still matter for recovering a watch
  that cannot hold an SDP session.
- **I-9 overlaps everything.** Nearly every N-series defect was a place Slate
  deviated from InfiniTime. Doing I-9 deliberately would likely retire issues
  not yet written down.
- **N-25 is fixed at the symptom, not the cause.** See P-8.

---

# Part II — The prompts

## A. The product thesis

### P-1 — Push a display list from the phone and render it on the watch

| | |
|---|---|
| **Status** | Open — **recommended next work** |
| **Area** | Companion compositor ↔ firmware `session` / renderer |
| **Depends on** | Nothing. All prerequisites are proven. |
| **Why now** | Every defect fixed so far was plumbing. The transport is proven end-to-end; the payload has never been exercised. |

**Prompt:**

The Slate link is now fully working: MTU 247, SDP session negotiated on
subscribe, heartbeats flowing, time sync landing, firmware updating over
channel 5. What has never been tested is the thing the whole architecture
exists for — the companion composing a display list and the watch rendering it.

Goal: get one deliberately simple display list from the phone onto the watch
screen, and one input event back.

1. Establish what already flows. The companion log shows `ch=1` (DISPLAY) and
   `ch=4` (SYSTEM) traffic during a live session. Find out what those messages
   are, whether the watch parses them, and whether anything reaches the
   renderer. Report what you find before changing anything.
2. Build the smallest end-to-end proof: a display list from the Kotlin DSL with
   a CLEAR, one filled rect and one text element, pushed on channel 1, taking
   over the screen from the local face.
3. Verify on hardware with the diagnostic overlay: the parse and render
   millisecond fields on line 2 should move, and `remote_depth` should become
   non-zero. Screen handover from local to remote must be visible.
4. Send one touch event back. Wrap the rect in `BEGIN_ELEM` with `EMIT_TOUCH`
   and confirm the element id arrives at the companion.
5. Confirm the golden-file rule still holds: the Kotlin DSL and the JS builder
   must emit **byte-identical** display lists. If no such test exists for this
   path, write it.

Constraints: the watch never executes anything received over BLE — display
lists are data only. Every BLE-facing parser validates all lengths, indices and
coordinates before use, and rejects and resyncs rather than faulting.
Dirty-rect rendering is mandatory: a full-screen redraw is ~115 KB of SPI
traffic.

Done when: a display list composed on the phone is visibly rendered on the
watch, a tap on it is received by the phone, and the round-trip is documented
with measured parse and render times.

---

## B. Companion and ops

### I-15 — Companion UX when the watch is not advertising

| | |
|---|---|
| **Status** | Open — expected limitation, UX unclear |
| **Area** | Companion CDM + link service |
| **Impact** | A user with a silent watch has no path forward and no explanation |
| **Priority** | High — the last remaining way to strand a user |

**Prompt:**

CompanionDeviceManager association and "Start / reconnect link" cannot find a
radio that is not advertising. During a soft-brick, an InfiniTime-only
recovery, or a watch sitting in the bootloader, both simply fail with no useful
message. This was a real source of confusion during bring-up.

1. Enumerate the states a watch can be in from the phone's point of view:
   advertising as Slate, advertising as InfiniTime, advertising as the
   bootloader/recovery, connected to another central (Gadgetbridge, nRF
   Connect), and genuinely silent.
2. Detect which one applies using what already exists — a scan,
   `LinkContention` instant GATT occupancy, and the CDM association record.
3. Give each state a specific, actionable message. "Not found" is never
   sufficient. A watch held by Gadgetbridge needs different advice from one
   that is flat.
4. Fold in the sealed-installer path: when the watch is only reachable as
   InfiniTime or recovery, steer to that flow rather than letting the user
   retry Slate association forever.
5. Surface it in the existing Troubleshooting screen and the in-app log rather
   than building a new UI surface.

Done when: each of the five states produces a distinct, correct explanation
with a next action, verified by reproducing at least three on hardware.

---

### I-7 — Nordic DFU reports failure on a successful swap

| | |
|---|---|
| **Status** | Open — cosmetic but misleading |
| **Area** | Companion DFU client / firmware reboot timing |
| **Impact** | A successful update looks like a failure |
| **Priority** | Medium — demoted now that SDP OTA is primary (I-13) |

**Prompt:**

When a Nordic legacy DFU completes, the watch reboots to swap the image. The
disconnect that follows is reported as a failure even though the update
succeeded. The same disconnect appears at the end of an SDP OTA as a `status=8`
supervision timeout, which the companion now handles correctly — use that as
the model.

1. Distinguish an expected post-transfer reboot from a genuine failure. The
   transfer having reached 100% with the last packet acknowledged is the
   discriminator.
2. Report "installed, watch rebooting", then wait for the reconnect and confirm
   success by reading the version or confirm-status from the rebooted watch.
3. Only report failure if the watch does not come back within a stated timeout,
   or comes back running the old image.

Done when: a successful Nordic DFU never shows an error, and a genuinely failed
one still does.

---

### I-8 — Harden sealed-DFU target classification

| | |
|---|---|
| **Status** | Open — heuristic |
| **Area** | Companion sealed-installer flow |
| **Impact** | Wrong target choice sends an image to the wrong device |
| **Priority** | Medium — demoted (see I-7) |

**Prompt:**

Deciding whether a discovered device is Slate, InfiniTime, the bootloader or
something else is currently heuristic, based largely on advertised name. That
is fragile: the same physical watch presents differently depending on what it
is running.

1. Classify on advertised service UUIDs rather than names where possible —
   Slate's own service, the Nordic DFU service (`0x1530`) and InfiniTime's
   characteristics are all discriminating.
2. Handle the case where the identity address is the same across firmwares (it
   is — the FICR-derived address is stable, see N-9), so a cached
   classification can be stale after a flash.
3. Refuse to start a transfer when classification is ambiguous, and say why,
   rather than guessing.

Done when: classification is UUID-driven, survives a firmware change on the
same address, and ambiguity blocks rather than guesses.

---

## C. Firmware and architecture

### I-9 — InfiniTime low-level divergence audit

| | |
|---|---|
| **Status** | Open — partially done, never completed systematically |
| **Area** | Firmware boot, BLE bring-up, peripherals |
| **Impact** | Unknown remaining divergences, each a latent defect |
| **Priority** | High — highest expected yield of anything on this list |

**Prompt:**

Nearly every defect in the N-series was a place where Slate deviated from
InfiniTime or from the stock NimBLE flow: N-9 (identity address), N-11 (tick
priority), N-12 (SAADC config), N-15 (who drives negotiation), N-16 (when
services are registered), N-17 (UUID byte order), N-23 (when the session
starts). Each was found by hitting it on hardware. The remaining ones are still
there.

`CLAUDE.md` states the standing rule: mirror InfiniTime for boot, MCUBoot/DFU,
flash map, WDT/button and BLE radio bring-up; differ only above that, at SDP
and companion level.

1. Audit against these axes specifically, because they are the ones that
   actually produced defects: **initialisation ordering**, **who initiates** an
   operation (watch or phone), **worst-case duration of work done inside a
   callback**, and **who writes peripheral ENABLE registers**.
2. Cover: reset and clock bring-up, GPIO and pin configuration, SPI and TWI
   setup and teardown, the NimBLE host/controller task topology and priorities,
   GAP/GATT registration order, advertising parameters and resume policy,
   connection parameter negotiation, and the watchdog.
3. For each divergence found, record: what InfiniTime does, what Slate does,
   whether it is deliberate, and the risk. Update `docs/infinitime-parity.md`,
   which currently has at least one row marked "aligned" that was not.
4. Fix the accidental ones. Leave the deliberate ones documented with a reason.

Done when: `docs/infinitime-parity.md` covers every axis above with no
unexplained divergence, and every accidental one is either fixed or has a filed
issue.

---

### P-8 — Flow control on the CONTROL channel

| | |
|---|---|
| **Status** | Open — new, arising from N-25 |
| **Area** | Firmware `ble_link` / `AppInbox`, companion `SdpWriteQueue` |
| **Impact** | Any multi-message CONTROL burst can silently lose messages |
| **Priority** | Medium — mitigated but not solved |

**Prompt:**

N-25 was fixed at the symptom. The underlying asymmetry remains: `AppInbox`
holds exactly one message and `ble_link` gates all ingest while it is busy,
withholding CREDIT. But the companion writes with **write-without-response**,
so it never sees the withheld credit and nothing retransmits. Anything sent
while the app task has not yet drained (a 20 ms window) is lost silently.

The time sync was lost this way and the clock stayed at 1970 for entire
sessions. It was only found because the clock was visibly wrong; a lost message
with no visible symptom would not have been.

1. Decide the model. Either the companion respects CREDIT on CONTROL as it
   already does on channel 5 (OTA), or CONTROL moves to write-with-response, or
   the inbox gains a small queue. State the trade-off in RAM (see I-14) and
   latency.
2. Make drops observable regardless. `note_busy_drop()` already exists —
   surface the count in the diagnostic overlay and report it to the companion,
   so a silent loss stops being silent.
3. Add a host test that bursts several CONTROL messages faster than the drain
   interval and asserts none are lost.

Done when: a CONTROL burst cannot silently lose a message, and the drop counter
is visible on the watch and in the companion log.

---

### I-14 — RAM headroom watch

| | |
|---|---|
| **Status** | Watch — 93% used, ~490 B below `__StackLimit` |
| **Area** | Linker / CI RAM gate |
| **Impact** | Static growth trips the gate or corrupts the stack |
| **Priority** | Medium — constrains every other prompt here |

**Prompt:**

The hard gate is static + heap ≤ 54 KiB with ≥ 6 KiB slack. The current prod
link map sits at 60968 B of 64 KB. The margin is thin enough that any new
static allocation needs a map check — and P-1 (display lists) will want
buffers.

1. Produce a current breakdown by symbol: what actually occupies RAM, largest
   first. The NimBLE heap was bumped historically for the event queue; verify
   that is still the right size.
2. Identify what can move to flash (const tables), what can be shrunk, and what
   is genuinely required.
3. Make the CI gate report the top consumers on failure, so a regression names
   the culprit rather than just failing.

Done when: there is a documented breakdown, at least 6 KiB of genuine slack,
and a CI failure that identifies what grew.

---

### I-3 — Tickless idle on RTC1: suppress-path review and soak

| | |
|---|---|
| **Status** | Open — disabled, never soak-tested |
| **Area** | FreeRTOS port, `port_rtc_tick.c`, power |
| **Impact** | Battery life; risk of reintroducing N-10 style tick loss |
| **Priority** | Medium — now testable, since the watch stays up for hours |

**Prompt:**

Tickless idle is off. It interacts directly with the RTC1 tick port, which has
already produced two serious defects: N-10 (tick loses time under BLE load,
leading to a watchdog reboot loop) and N-11 (tick IRQ running at priority 0
because a raw priority was double-shifted).

1. Review the suppress path: how long sleep can be extended, how the catch-up
   is bounded (`kMaxCatchUp` is 128 with a top-half-range guard), and what
   happens when the radio wakes the CPU mid-sleep.
2. Confirm the WDT interaction. The tick and idle hooks deliberately do **not**
   pet the watchdog — only the app task loop does. A tickless path that sleeps
   longer than the ~7 s dog without the app task running would reset the watch.
3. Soak with the diagnostic overlay visible: the tick catch-up count (line 1,
   last field) must stay at 0, and worst phase 6 must not grow.
4. Measure the actual current saving before deciding it is worth the risk.

Done when: a multi-hour soak shows zero tick catch-up and no watchdog resets,
with a measured current figure justifying the change — or the item is closed as
not worth it.

---

## 5. Suggested execution order

1. **P-1** — push a display list. Everything else is infrastructure for this,
   and it is the only item that tests the architecture rather than the
   plumbing.
2. **I-15** — the last remaining way to strand a user with no explanation and
   no path forward.
3. **I-9** — the divergence audit. Highest expected yield: it finds defects
   before hardware does, which is the opposite of how the last twenty-six were
   found.
4. **P-8** and **I-14** together — both constrain how P-1 can be built, and
   both are cheaper to address before more code depends on the current shape.
5. **I-7** and **I-8** — Nordic DFU polish, now a recovery path rather than the
   main one.
6. **I-3** — tickless, only after a soak proves it is safe and a measurement
   proves it is worth it.

---

## 6. Closed — no action

**I-13 — channel-5 SDP OTA.** Proven on hardware 3 August, twice, with no
InfiniTime round-trip. 134180 B at ~46 kB/s, zero NAKs, zero resyncs, IMAGE_OK
confirmed after the connected dwell. The deliberate-failure rows of
`docs/ota-verification.md` (mid-transfer disconnect, corrupt chunk on the wire,
refusal while unconfirmed) remain proven only in host tests — worth running on
hardware eventually, but not blocking.

**I-6 — IMAGE_OK confirm vs single BLE connection.** Resolved. The watch
confirms after a dwell with a central connected, and the companion now logs the
ON TRIAL → CONFIRMED transition explicitly.

**I-12 — GATT CTS.** Recommend closing. TIME_SYNC on CONTROL carries local
wall-clock and works; a CTS client would duplicate it.

**I-1, I-2, I-4, I-5, I-16** — closed in earlier sessions and verified on
hardware since.

**N-1 … N-26** — all closed. N-19 to N-26 are documented in `issues.md`: OTA
credit desync, whole-slot erase, bus mutex under an in-flight write, app task
contention during transfer, session start on subscribe, duplicate connect,
discovery watchdog, dropped CONTROL burst, and the UTC clock.
