# EvoTime — open work register

> **This file is the single point of truth for open work.** `issues.md` is older
> and now partial; where they disagree, this file wins. Capability inventory
> (what works today): [`capabilities.md`](capabilities.md). Updated
> **10 August 2026**.

---

## Current state (10 Aug 2026)

### Companion installed + dual ZIP Open-with

Pixel has **`0.8.2-p38` / versionCode 39** (`slate.app.debug`). Opening a `.zip`
with Slate classifies via `ZipIntake`: JS sub-app → sideload; Nordic/Slate DFU
(`manifest.application` or `slate-mcuboot-image.bin`) → SDP OTA with URI
pre-selected. Sealed InfiniTime install remains the dedicated main-screen flow.

### N-59 / N-60 — CONFIRMED on hardware (operator, after `5543D0BF9804`)

**Raise-to-wake works** (display wakes on wrist raise). **Steps work**, non-zero
on the face — so the pedometer enable path and the axis swap both landed.
Accuracy after ACC_CONF `0x28`: **operator reports acceptable** (10 Aug).

**False-wake:** operator confirmed no wake on typing / arms-down walk — nothing
further needed.

**Firmware ready to flash:** `build/dfu/slate-dfu.zip` — SHA-256 prefix
`23BF8499CA72` (face stamp ~`06:13`). Includes Face-only settings routing,
remote RIGHT → `local_back`, no false “Not connected” while GATT is up, and
**rounded settings buttons** with On/Off / timeout / Never on the right.

### Swipe settings ↔ launcher (firmware routing bug)

On settings, swipe right→left opened the launcher (phone) instead of the face;
on the launcher, swipe left→right opened settings instead of closing to the face.

Cause: `main.cpp` always stole RIGHT for `show_settings()`, even when a remote
screen owned the panel — so LauncherApp never saw its close gesture. LEFT from
settings fell through to the phone.

**Partial fix (in `9421B271FDC3`):** Settings+LEFT → face. Residual: RIGHT still
opened settings from the local **Disconnected** (“Not connected”) screen, and
that screen appeared while the phone was already GATT-connected but SDP not
Ready yet.

**Follow-up (10 Aug):** RIGHT → settings **only from Face**; remote RIGHT does
`local_back` + notify phone; launcher swipe while `link_up` / Connected no longer
shows “Not connected”.

**Follow-up 2 (10 Aug, same day):** that Connected forward path was still a
silent no-op — `InputRouter::emit` dropped every message unless the session was
already Ready/Active/Idle, so GATT-up / HELLO-in-flight swipes never left the
watch. Connected is now allowed through `emit`. Disconnected (“Not connected”)
also swallowed both horizontal swipes (RIGHT was the anti-settings `continue`,
LEFT re-entered not-connected); either direction now returns to the face.

### N-59 / N-60 — steps and raise-to-wake, both dead, both accelerometer (history)

**Firmware that carried the fix: `5543D0BF9804` (stamp 13:47).** Superseded
`1560AB3C3466`. 22/22 host tests (`-E ble_link`). RAM slack 200 B.
**Companion build 38 (`0.8.2-p37`) installed.**

Reported after flashing `1560AB3C3466`: step counter 0, wrist raise does not
wake, both enabled in both settings screens. Charger-edge wake works, the watch
settings screen works, the phone screen works.

The two failures share one dependency and the one working feature does not use
it. Both were found by reading Bosch's driver and InfiniTime against Slate's —
no hardware, no diag round trip.

#### N-59 — the step-counter enable bit never reached the sensor

`enable_step_counter()` read-modify-wrote the 70-byte feature block in **8-byte
chunks with the ASIC address set once**. The address does not survive the end of
a transfer. Bosch's `bma4_read_regs` calls `increment_feature_config_addr()`
after **every** chunk, and takes a single transfer when it can.

So all nine chunks addressed bytes 0–7:

- the enable bit went into a copy of byte 3, never byte 0x3B;
- the write-back scribbled nine chunks over the head of the config;
- the verify re-read the same wrong bytes, saw the bit it had just put there,
  and **reported success**.

`diag_bma_step_en` would have read 1 the whole time. This is the second time
this driver has reported success for a pedometer that was switched off — the
first was the config upload (N-54), the same class of bug one layer up.

Fixed: one 70-byte read and one 70-byte write, Bosch's non-chunked path.
`bma_write`'s buffer was already sized 80 "because the BMA feature block is a
single 70-byte write" — the transfer that comment describes had never been made.

#### N-60 — the accelerometer axes were in the chip's frame, not the watch's

The BMA is mounted rotated in the PineTime. InfiniTime's `Bma421::Process`
returns `{steps, data.y, data.x, data.z}` — its X is the sensor's Y — and
`MotionController`'s thresholds are written in that frame. `RaiseDetector`
copies those thresholds exactly but was fed `read_accel`'s unswapped output.

The first test in `ShouldRaiseWake` rejects when `|xMean| > 384`. In the chip's
frame that axis sits near ±1 g (≈±1024) on a wrist, so **every gesture was
rejected**. Raise-to-wake could not fire at any angle.

Fixed in `read_accel`, mirroring InfiniTime, so the swap is a property of the
mounting rather than of the detector. It is the only consumer.

#### `tests/host/test_bma42x.cpp` — new, and the reason both were found

There was no test for this driver at all, which is how a function that verified
its own work still shipped broken twice. The new one runs the driver against a
sensor model that enforces the rule the code broke: FEATURES_IN reads and writes
at whatever 0x5B/0x5C name, and **the address does not persist across
transfers**. It reproduced both defects before the fix.

One trap worth recording: the first fixture filled the block with `feature[i] =
i`, so byte 0x3B held 0x3B — which already carries the 0x10 enable bit. The
assertion passed against the broken driver. The fixture now clears the bit and
asserts it starts clear.

#### What to look for after flashing

- **Steps.** Walk a hundred paces. Non-zero means the pedometer is finally on.
- **Raise.** With the screen asleep, raise your wrist to read the watch. It
  should wake, and should NOT wake from typing or walking arms-down.
- **Disproof for steps:** still 0 means the bit lands but the feature engine is
  not running — next look would be ACC_CONF (now matched to InfiniTime `0x28`)
  and whether advanced power save should be left on.

### Companion watch-settings screen — layout fixed

Two faults visible in the operator's screenshot:

- The first row was clipped under the system bar. `targetSdk 35` draws
  edge-to-edge and the screen had no inset padding — the exact trap already
  recorded in CLAUDE.md's working practice. Fixed with `safeDrawingPadding()`.
- Six chips in a plain `Row` did not fit, so "Never" was compressed to one
  character wide and rendered as a **vertical column of letters**. Fixed with
  `FlowRow`, which wraps instead of compressing at any width or font scale.

Also: dropped the in-content headline duplicating the title bar, grouped each
setting into a card, and switched the toggle rows from a fixed `fillMaxWidth(0.75f)`
to a weighted column so the switch sits at the edge on any screen.

### N-58 — the reconnect button refused to reconnect, and blamed an app that was not there

**Companion build 37 (`0.8.1-p36`) installed.** 168/168 `:sdp-tests`.

Reported as "unable to reconnect the watch in the companion app", with the
operator stating no other app was interfering. They were right, and the app was
telling them otherwise.

**Evidence** (`adb shell dumpsys bluetooth_manager`):

```
stack::gatt  2026-08-09 11:47:53.522 …08:89, BT_TRANSPORT_LE, state: GATT_CH_OPEN, No ACL holders
```

An LE link to the watch, **open and owned by nobody** — the last GATT event on
record, never closed. `ps` confirmed nRF Connect was not running.
`getConnectedDevices(GATT)` therefore returned the watch while Slate was not
connected, and `LinkContentionLogic.evaluate` reported "Another app on this
phone holds the watch BLE link".

That claim is not something the signal can support. The same signal appears when
a link is left orphaned, which is what happens when the app is **reinstalled or
force-stopped while connected** — the process dies, its GATT clients are
unregistered (`REASON_UNREGISTER_CLIENT` in the same dump), and the ACL stays
up. On a development phone that is the *likeliest* cause, and no amount of
closing Gadgetbridge fixes it.

**The lockout.** [MainActivity.kt](../companion/app/src/main/java/slate/app/MainActivity.kt)
had three sites that `return`ed on a contention verdict, including
"3. Start / reconnect link service". The logged sequence matches exactly:

```
12:49:15.175 I startObservingDevicePresence(E8:01:34:22:08:89)
12:49:15.179 W LinkContention: Another app on this phone holds the watch BLE link
```

— `startObservingPresence`, then the verdict, then nothing. The service was
never started. The one action that could have recovered the link was the one
being refused, and no path in the app could clear the stale connection.

`LinkForegroundService.connectAddress` has always logged the same verdict and
connected regardless. The UI was the outlier.

**Fixed:**

- All three MainActivity sites warn and proceed. If a rival really does hold the
  slot, the connect fails and the reconnect watchdog reports it — strictly more
  informative than declining to try.
- The verdict says what the signal proves: "This phone already has a BLE link to
  the watch that Slate does not own." Pinned by
  `heldOnThisPhoneDoesNotBlameAnotherApp`, which asserts the summary does *not*
  contain "another app".
- `LinkContention.STALE_LINK_TIP` leads the remediation copy: turn Bluetooth off
  and on to drop an orphaned link. It is the only remedy that addresses this
  cause and it was absent from a list of three app-specific ones.

**OTA and sealed-DFU still block on contention, deliberately** — flashing over a
contended link on a sealed watch is the one place where refusing to start is the
right answer.

**Not caused by the settings work**, but triggered by installing build 36 over a
live connection. Worth knowing for every future install: reinstalling while
connected can orphan the link.

### Watch settings, on both devices, synced — built, NOT yet on hardware

**Firmware to flash: `1560AB3C3466` (stamp 12:34).** Supersedes `A987839D42A0`,
`DC04B7288671`, `88DBB925E697` and `451C3AB52718`, and contains everything
before them. 21/21 host tests (`-E ble_link`). RAM slack 200 B.

**Companion: build 36, `0.8.0-p35`, installed on the Pixel.** 167/167
`:sdp-tests`.

Three settings, editable in both places, syncing both ways:

| | watch | phone |
|---|---|---|
| Raise to wake | Settings row 1 | switch |
| Screen timeout | Settings row 2 (cycles) | chips 10/20/30/60/120/Never |
| Show step count | Settings row 3 | switch |

Reached on the watch by **swiping left-to-right** from the face; on the phone
from **Watch settings** on the main screen.

`tilt_sensitivity` is deliberately **not** synced and not shown: it configured
the any-motion threshold that raise-to-wake replaced, so it does nothing. A
control with no effect is worse than no control.

#### The merge rule, and why it is written twice

`include/settings_sync.hpp` / `src/settings_sync.cpp` and
`companion/sdp-core/…/session/WatchSettings.kt` are the same 10-byte format and
the same rule, one per language. Wire:
`[0x21][ver][revision:u32 LE][tilt][wake_s][steps][reserved]`.

- Higher revision wins.
- Equal revision, identical content: no-op.
- Equal revision, **different** content is a real conflict — both ends edited
  without seeing each other. The **watch wins**; each side reaches that verdict
  alone from its own `self_is_watch`, which is what stops them overwriting each
  other forever.
- A local edit is stamped `max(mine, highest_seen) + 1` — a Lamport clock, not a
  sequence number. That is the whole reason last-writer-wins still holds after
  both have been offline and edited. Saturates at `0xFFFFFFFF` rather than
  wrapping, since a wrap would resurrect ancient settings.

The Kotlin tests pin **the firmware's own encoder output**, not a re-derivation
of it: the golden vectors in `WatchSettingsTest` came from running
`slate::settings_sync::encode`.

Opening exchange: the **phone speaks first**, 1200 ms after the session goes
Ready (N-25 — the watch's AppInbox holds one message, and 1200 ms sits clear of
the time-sync retries at 300/2300/10300 ms). The watch only volunteers its copy
when it has an unsent edit, so without the phone opening, two sides that had
both changed would sit on different values indefinitely.

#### N-57 — the settings rows were drawn but their taps went to the phone

`Core::on_tap_elem` had **no caller**. The rows were laid out correctly, carried
`EMIT_TOUCH` and the right element ids, and the display-list test passed — but
in `main.cpp` a tap on a locally-owned screen went to `g_input.on_event`, which
encodes it as an SDP input event and sends it to the companion. The phone has
never heard of `kSettingRaise`, so the tap vanished. On the wrist this would
have read as "tapping the row does nothing", which is indistinguishable from a
touch-driver fault and would have cost a flash cycle to chase.

Found by grepping for callers before packaging, not on hardware.

Fixed by `local_tap_handled()` in `main.cpp`: hit-test against the rects the
interpreter already collected while rendering the local list (`core_push_list`
calls `set_hits`, so no second table is needed), then hand the id to Core.
`Core::on_tap_elem` now returns **bool** so the screen check stays in the one
place that knows which local screens have live elements — an unclaimed tap, or
one on any other screen, still reaches the router exactly as before.

Pinned by `test_settings_taps_reach_their_setting`, which walks the list Core
actually pushes, taps the centre of each touchable element, and asserts the
setting that row displays is the one that changed. A test that only counts
opcodes cannot see this class of bug.

#### N-56 — the revision was RAM-only, so every reboot lost the argument

Found while wiring the phone half, not on hardware. `settings_rev_` lived in
`Core` and nowhere else, so it returned to **0** on every restart while the
phone kept counting. On the next connection the phone therefore outranked the
watch and quietly put its own **older** copy back: a setting changed on the
watch undid itself. The watch reboots on every flash, so this was not rare.

Fixed by moving the revision into `local::Settings`, which is persisted — magic
bumped `'SLTT'` → `'SLTU'`, so **settings return to defaults once** on this
flash. `highest_seen_rev_` stays in RAM and is seeded from the persisted
revision on load, which is the correct floor.

Pinned by `test_settings_revision_survives_reboot` in `test_local_core.cpp`,
which rebuilds a `Core` over the same persisted slots and then offers it a stale
phone copy.

#### What to look for after flashing

- **Swipe left-to-right from the face** → three labelled rows with On/Off and a
  timeout value. Tapping a row changes it.
- **Change one on the watch, open Watch settings on the phone** → it should
  already show the new value.
- **Change one on the phone** → the watch's settings screen shows it, and the
  behaviour follows (timeout is the easiest to feel).
- **Change one on the phone with the watch out of range**, then reconnect → the
  phone screen says "Waiting for the watch" until it lands.
- **Disproof:** a value that reverts a few seconds after being changed means the
  two ends are both applying each other — the tie-break is the thing to look at,
  and `LinkLog` prints every send and receive with its revision.

### I-19 — RAM margin (partial reclaim)

Was 176–200 B link margin (`__StackLimit` − `__heap_end__`). `ScreenBlock` was
a fixed **3072 B** with ~2.9 KiB empty `reserve` around a ~152 B `State`.

Reclaim: `kLocalScreenStateBytes` **3072 → 256** (`local_budgets.hpp`). Saves
~2816 B static RAM. Post-reclaim link margin from `build/dfu/slate_firmware.map`
(9 Aug DFU): **`__StackLimit` − `__heap_end__` = 3016 B**; RAM ~89.15% (was
~93.5% / 176 B). `g_core` shrinks with the block (~3568 B in `.data`).

Supporting numbers, measured from `build/dfu/slate_firmware.map` (pre-reclaim,
8 Aug):

| | bytes | section |
|---|---|---|
| `ucHeap` (FreeRTOS heap_4) | 16384 | .bss |
| `g_interp` | 8600 | .bss |
| `g_renderer` | 7696 | .bss |
| `g_core` | 6380 | **.data** |
| `g_link` | 4252 | .data |
| `persist_nvmc::g_page_cache` | 4096 | .bss |
| **total RAM** | **61264 / 65536 = 93.5%** | |
| **`__StackLimit` − `__heap_end__`** | **176** | link margin (pre-reclaim) |

Notes for whoever picks this up:

- `g_core` sits in `.data` rather than `.bss` purely because of its non-zero
  member initialisers, so it costs flash for the initialiser image as well.
  Zero-initialising the few fields that do not need a non-zero default would
  move the whole 6380 B object to `.bss`.
- The 176 B is unallocated margin between the top of statics and the bottom of
  the MSP stack. The FreeRTOS heap is a fixed `.bss` array and does not grow
  into it, so the failure mode is `ASSERT (__StackLimit >= __heap_end__)` at
  link time — loud, not silent.
- **Do not chase `GlyphCache`.** Its 6144 B static looks like an easy win in the
  map and is not: `--gc-sections` already discards it. A parse that "found" it
  on 8 Aug was reading the map's *discarded* sections. Confirmed by excluding
  the file from the build and observing zero change.
- `CLAUDE.md` states "total static + heap under 54KB, >=6KB slack. CI enforces."
  Neither figure currently holds. Either the gate is measuring something else or
  it is not running — worth establishing before treating 6 KB as the target.

### N-55 — steps still 0 on `839EEED31683`; two more causes found

**Firmware to flash: `070AC047BE3E` (stamp 15:57).** Supersedes everything
before it. 20/20 host tests. RAM slack 200 B.

The blob upload fix was necessary and not sufficient. Two further faults, both
found by reading InfiniTime's `Bma421.cpp` init sequence rather than the symptom:

1. **The pedometer was never switched on.** InfiniTime calls
   `bma423_feature_enable(BMA423_STEP_CNTR, 1)` *after* loading the config.
   Slate's `enable_step_counter()` set a C++ bool and told the sensor nothing.
   Loading the stream boots the feature ASIC; it does not enable any feature.
   Now a real 70-byte read-modify-write of the feature block, setting
   `BMA423_STEP_CNTR_EN_MSK` at `BMA423_STEP_CNTR_OFFSET + 1`, with the ASIC
   address registers restored first (the config upload leaves them pointing
   past the end of the stream, and Bosch reads them back rather than assuming).
2. **`bma_write` in main.cpp had a 16-byte buffer** and returned false for
   anything larger. The feature block is a single 70-byte write, so step 1
   would have failed silently even once written. Now 80.

**Instrumented, as promised rather than guessed at.** Diag line 3 gains a BMA
group: `…/<chip>.<internal_status>`.

- chip: 0 undetected, 1 = BMA421, 2 = BMA425
- status: the sensor's `INTERNAL_STATUS` (0x2A) after the upload. **1 means the
  feature ASIC booted.** 0xFF means no upload was attempted (chip undetected),
  0xFE means the status read itself failed.

That one byte separates "the blob never loaded" from "you have not walked",
which is the ambiguity that has made this bug expensive.

### Display timeout is now 20 s (owner request)

`settings.wake_seconds` default 5 → **20**, clamped 5..120, 0 disables.

Two things had to change for that to take effect, and the second is the
interesting one:

- `kSettingsMagic` bumped 'SLTS' → 'SLTT'. `load_settings()` only applies
  defaults when the magic mismatches, so without a bump the new default would
  never reach a watch that already had a settings block stored — including this
  one. Cost: customised tilt settings revert once. There is no settings UI yet,
  so nothing has been customised.
- **`load_settings()` restated every default itself**, including
  `wake_seconds = 5u`, so changing the struct's member initialiser did nothing
  at all. Defaults now live in `local::Settings` and nowhere else. This was
  caught by the host test pinning 20 s, not by reading the code.

### Step counter + raise-to-wake — built, NOT yet on hardware

**Firmware to flash: `839EEED31683` (stamp 15:33).** Supersedes `94FE87EC9178`
and contains everything before it. 20/20 host tests.

#### N-54 — the step counter never worked, and the reason was the upload

`Core::init` passed `feature_cfg = nullptr`, so no Bosch config stream was ever
sent. But fixing only that would not have helped, because
`write_feature_config` was itself wrong — and it is worth stating exactly how,
since it returned success the whole time:

| Bosch `bma4_write_config_file` | Slate had |
|---|---|
| advanced power save OFF, 450 us settle | missing |
| INIT_CTRL = 0 | present |
| **per chunk: ASIC address to 0x5B / 0x5C**, then bytes to 0x5E | **missing — every chunk went to the same address** |
| INIT_CTRL = 1, 150 ms | present |
| **verify INTERNAL_STATUS (0x2A) == 1** | read a different register and ignored it |
| advanced power save ON | missing |

Without the address registers the whole 6 KB lands on top of itself and the
feature ASIC never boots. The function then returned `true` regardless, so
`enable_step_counter()` set `step_enabled_ = true` and `read_steps()` happily
reported the zeros the sensor was giving it. **That is the entire history of
"steps read 0" on this project.**

Both blobs are now vendored at `third_party/bosch/bma_config.{cpp,hpp}` —
6144 B each, BSD-3-Clause, Bosch notice retained verbatim, `const` so they cost
flash (+12.4 KB) and no RAM. The 421 runs the 423 stream, the 425 its own,
picked from the runtime chip detection.

**No new diag field for "did the blob load".** The step count is already the
honest indicator: non-zero after a walk means the ASIC came up, a permanent 0
means it did not.

#### Raise-to-wake ("wrist flick")

`slate::motion::RaiseDetector` — `include/raise_wake.hpp`, `src/raise_wake.cpp`,
17 host tests in `tests/host/test_raise_wake.cpp`.

Mirrors InfiniTime's `MotionController`: 8-sample history at a 100 ms poll
(0.8 s), mean of the newest two against the oldest two, variance over the
newest two, then the four thresholds and `DegreesRolled`. Integer `asin` by
binary search over a sine table scaled to 32767 — a **second** table, not the
renderer's, because that one is scaled to 256 for pixel work and its top entries
do not reach 256, so it cannot be inverted accurately.

**It does not use the BMA's wrist-wear interrupt, and neither does InfiniTime.**
That feature lives in the same ASIC the config blob boots; raw acceleration is
live regardless. Statistics over raw samples work on any part in any state and
can be inspected and tuned — an opaque interrupt cannot. The old `poll_tilt`
used the sensor's **any-motion** interrupt, which fires on any movement above a
threshold and is not a gesture at all; it is gone.

Polled **only while asleep**. Awake, the screen is already lit and the samples
would buy nothing but an I2C transfer every 100 ms, and the history is kept
empty so every sleep starts cold rather than full of whatever the wrist did
while the user was reading.

Gated on the existing `settings.tilt_enabled`, so flick can be turned off
without a reflash — which was the point of asking whether it should be a
setting.

#### RAM: 176 bytes of link margin, down from 432

This session's firmware work cost ~256 B of static RAM (the sleep state, the
detector's 48-byte history, and padding in `g_core`, which is `.data` because of
its non-zero member initialisers).

Failure mode is a **link-time assert**, not runtime corruption — the gap is
unallocated margin between `__heap_end__` and `__StackLimit`, and the FreeRTOS
heap is a fixed 16 KB `.bss` array that does not grow into it. But it is thin,
and `CLAUDE.md`'s stated ">=6 KB slack" has not been met for some time: RAM is
at 61264 / 65536 = 93.5%, against a stated budget of 54 KB.

**A false lead, recorded so nobody repeats it.** The map appeared to show
`GlyphCache::alloc_bytes`'s 6144-byte static occupying `.bss`, which would have
been an easy 6 KB win. It does not: `--gc-sections` was already discarding it,
and the parse that "found" it was scraping the map's *discarded* sections. The
register's older claim that GlyphCache "contributes 0 bytes to .bss today" was
right. Reclaiming RAM needs a real look at `ucHeap` (16 KB), `g_interp`
(8.6 KB), `g_renderer` (7.7 KB) and `g_core` (6.4 KB) — its own task.

#### What to look for after flashing

- **Steps.** Walk a hundred paces and look at the face. Non-zero means the blob
  loaded and the ASIC booted — the single most informative check here.
- **Flick.** With the screen asleep, raise your wrist to read the watch. It
  should wake. It should NOT wake from typing, walking with arms swinging, or
  putting the arm down.
- **Disproof:** steps still 0 means `INTERNAL_STATUS` is not reading 1 and the
  stream is still not loading — the next step would be logging that status byte
  rather than guessing again.

### Display sleep — built, NOT yet on hardware

**Firmware to flash: `94FE87EC9178` (stamp 12:54).** Supersedes `6622C8E8454D`;
it contains the N-53 band-blanking fix as well, so flash this one.

Mirrors InfiniTime's structure — the research is written up in
`docs/infinitime-parity.md` under "Sleep mode". 20/20 host tests, RAM headroom
unchanged (`__heap_end__` 0x2000ee50 vs `__StackLimit` 0x2000f000).

| | |
|---|---|
| `Core::Power { Running, Sleeping }`, timeout, `enter_sleep`, `wake_display` | `local_core.cpp` |
| `display_sleep` hook → `st7789::sleep_in/out` | `main.cpp::core_display_sleep` |
| Double-tap / button wake, activity stamping | `main.cpp` input loop |
| 9 host tests | `tests/host/test_local_core.cpp` |

**Two states, not InfiniTime's four.** `GoingToSleep` exists there because
`DisplayApp` and `SystemTask` are separate tasks and the handover is
asynchronous. Slate does it all on the app task, so the transition cannot be
interrupted and the extra states would carry no information. `AODSleeping`
needs an always-on mode Slate does not have.

**Wake sources**, all funnelling through `wake_display()` — the same
choke-point argument as `show_current()`, so a new source cannot forget to
leave sleep:

- side button
- **double tap** (`EventType::MultiTap`)
- wrist tilt (`poll_tilt`, already wired)
- charge/discharge edge (`poll_battery`, already wired)
- alerts (`enter_alert`)

**Single tap deliberately does not wake.** The CST816S is left in normal mode
while asleep — InfiniTime skips `touchPanel.Sleep()` for exactly this reason —
so it still reports every brush against a sleeve. The waking event is also
**consumed** rather than acted on, so the tap that wakes the watch cannot also
press whatever was underneath it.

**Charging does not hold it awake**, which is what was asked for and is also
what InfiniTime does: the charge edge wakes it so the change is visible, then
the ordinary timeout applies.

Timeout is the existing `settings.wake_seconds`, clamped to 5..120 s, 0
disables. Default is 5 s, which is short — worth raising after a wear test.

#### Deliberately NOT done, with reasons

- **SPI master + external flash sleep.** InfiniTime does both. Slate does
  neither, because (a) the flash shares this bus and an SDP OTA can arrive
  while asleep, with `ota_slot` writing from the app task, and (b) InfiniTime
  itself guards flash sleep behind a bootloader version check commented "avoid
  bricked device". On a sealed watch with no SWD that is not worth microamps.
  The backlight and panel are where the current actually goes.
- **`configUSE_TICKLESS_IDLE`.** Still 0. The app loop is the only thing that
  pets the watchdog and the bootloader dog is ~7 s; **I-3** already demands a
  soak first. Not bundled into this flash on purpose.
- **InfiniTime's `ShouldRaiseWake` statistics.** Slate uses the BMA's hardware
  tilt interrupt instead, which predates this work. A divergence, but a
  reasonable one; revisit only if tilt wake proves unreliable in wear.

#### What to look for after flashing

- Screen goes dark ~5 s after the last input; double tap or the button brings
  it back, and the **first** double tap only wakes — it must not also activate
  whatever was under your finger.
- A single tap should NOT wake it. If it does, the CST816S is reporting
  `MultiTap` for single taps and the gesture decode needs checking.
- On the charger: it should still sleep. Plugging/unplugging should wake it
  briefly.
- **Disproof:** if the screen wakes blank and stays blank, the panel is coming
  out of SLPIN but nothing is repainting — that would mean the digest is not
  being cleared on wake, and `test_display_sleep` is asserting the wrong thing.

### I-18 — battery % jumps down when the charger comes out — NOT A DEFECT

**Open only as a question of taste. Nothing is wrong, and nothing has been
changed.** Recorded here so the next person does not re-derive it, and — more
importantly — does not "fix" it by accident.

**Observed (8 Aug, 08:44, two readings 8 s apart):** removing the watch from
the charger dropped the battery bar from **62% to 51%**.

**Why.** `battery.cpp::apply_hysteresis` is a ratchet:

```cpp
if (power_present) return raw > prev_pct ? raw : prev_pct;   // charging: up only
return raw < prev_pct ? raw : prev_pct;                       // discharging: down only
```

A charger pulses current, so terminal voltage fluctuates. Every upward blip
ratchets the displayed value and it never comes back down while the cable is
in, so over a long charge the display creeps to the highest instantaneous
reading. Unplug, the ratchet flips direction, and the true resting value is
admitted at once.

The arithmetic from the two readings, both internally consistent:

| | on charger | off charger |
|---|---|---|
| voltage | 3815 mV | 3796 mV |
| `percent_from_mv` says | ~54% | **51%** |
| displayed | **62%** | **51%** |

`48 + (3796-3776) * 31/203 = 51`, exactly. The 62% is a latched peak from
earlier in the charge, not a reading of 3815 mV.

**It is InfiniTime's behaviour, verified line by line** (`CLAUDE.md` requires
checking there first, and this is why):

| | InfiniTime `BatteryController.cpp` | Slate `battery.cpp` |
|---|---|---|
| ratchet | `(isPowerPresent && newPercent > percentRemaining) \|\| (!isPowerPresent && newPercent < percentRemaining)` | identical |
| curve | `{3500,0},{3616,3},{3723,22},{3776,48},{3979,79},{4180,100}` | identical, same six points |
| charge clamp | `min(approx, isCharging ? 99 : 100)` | identical |
| voltage filter | **none** — one raw SAADC sample straight into the curve | identical |

An InfiniTime PineTime does the same jump on the same cell. Slate is a faithful
mirror, so changing it is a **deliberate divergence** and needs a written
reason under the standing rule.

#### If we do decide to change it

Ranked, with the trade-off stated:

1. **Filter the voltage before the curve** (recommended). A 4-8 sample moving
   average or median in `battery.cpp`, applied to the mV reading. This attacks
   the actual cause — the ratchet latching a single instantaneous peak — while
   leaving the ratchet, the curve and the clamp exactly as InfiniTime has them.
   The ratchet exists to stop the number oscillating and is worth keeping.
   Cost: a few bytes of state, and the displayed value lags a real change by a
   few sample periods. Note the sample cadence first: `Core::poll_battery`
   samples on a charge edge or every 10 s, so a multi-hour charge takes
   hundreds of samples and has hundreds of chances to latch a peak — the filter
   is doing real work here.
2. **Settle delay after unplug.** On the power-present falling edge, hold the
   displayed value for a few seconds before admitting a new one, so the cell
   recovers from charge-current elevation and the drop lands smaller. Cheap,
   but it treats the symptom and the number is knowingly stale meanwhile.
3. **Ease the displayed number down** over a second or two. Purely cosmetic:
   the jump becomes a slide. It briefly shows a value that is not the
   measurement, which is the sort of thing this project has been careful not to
   do on the diagnostic surfaces.

**What NOT to do:** remove the ratchet. Without it the percentage oscillates
with every current pulse, which is worse than one honest step, and it is why
InfiniTime has it.

Whatever is chosen, update the **Battery %** row in `docs/infinitime-parity.md`
from "aligned" to a documented divergence, with the reason.

### N-53 — a band-only push blanked the other 25 tiles — FIXED (firmware)

**Firmware to flash: `6622C8E8454D` (stamp 07:20).** The first firmware change
in this run of work; everything before it was companion-only.

Reported as "the watch face flashing, i.e. disappearing for a split second,
every now and again". An earlier instance guessed a 60 s full redraw. There is
no such timer, and that guess was wrong.

**Measured, on the desktop, with no flash spent** — `tests/host/test_band_push_tiles.cpp`:

```
before:  band y=56..96 -> 30 of 30 tiles flushed, 25 of them all-black
after:   band y=56..96 ->  5 of 30 tiles flushed,  0 of them all-black
```

`render_retained_to_display()` walks all 30 tiles, scrubs each to black, parses
the retained list into it and calls `flush_filtered_tile()`. That flush was
**unconditional** — it ignored the dirty tracker `put_pixel` has maintained
since M1, and which the legacy `flush()` has always honoured. So a band-only
list — the clock band, the diagnostic band, the OTA banner — was DMA'd as
black over the 25 tiles it does not cover. The face came back on the next full
repaint, which is a fraction of a second later. That is the flash.

It is occasional rather than every 2 s because the gap between the band push
and the next full repaint is loop-timing dependent: usually a few ms and
invisible, occasionally long enough to see.

Two changes:

| | |
|---|---|
| `flush_filtered_tile()` skips a tile the list drew nothing into | `renderer.cpp` |
| `clear_tile_buffer()` no longer marks content; a list's own CLEAR uses the new `fill_tile()`, which does | `renderer.cpp`, `sdp_interpreter.cpp` |

That split matters and the test caught it: with only the first change, a
full-screen CLEAR drew nothing at all, because `DrawSink::clear` scrubs the
buffer through the same call the render loop uses.

**Also fixed, and worth more than it looks:** `Core::show_current()` now skips
the push when the display list it just built is byte-identical to the last one
Core pushed (a 4-byte FNV digest — there are only ~432 B between `__heap_end__`
and `__StackLimit`, so a second 512 B buffer does not fit). Several callers
repaint unconditionally because they cannot cheaply tell whether their change
is visible; `set_remote_stale()` is called from the app loop **every 200 ms**
with a flag that almost never changes, and `apply_cts_time()` repaints on every
time sync. The digest answers that for all of them at one choke point. It is
invalidated on every band/banner push and on every screen-ownership change, so
a skip can never strand a band or a phone screen on the panel.

Expect this to move **N-36**: diag line 1 read `stall_events` climbing by 180
in 45 s — four stall episodes per second against a 230 ms render.

**Flashed and measured on hardware (8 Aug, 08:18 → 08:39, 1218 s of uptime):**

| Diag line 1 | before (45 s window) | after (1218 s window) |
|---|---|---|
| stall episodes | 336 → 516 = **4.0/s** | 63 → 716 = **0.54/s** |
| worst single stall | **714 ms** | **310 ms** |
| worst phase (line 2) | phase 7 @ 472 ms | phase 1 @ 308 ms |
| max render | 230 ms | 239 ms |
| inbox drops (line 3) | 3 in 45 s | 1 in 20 min |

**Stall episodes down 7.4x; worst single stall less than half.** The worst phase
moved off the paint path — phase 7 at 472 ms is gone. Max render is unchanged
at ~239 ms and should be: it is a max-since-boot for a genuine full repaint,
and full repaints still cost what they cost. What changed is how many happen.

This is the largest movement **N-36** has seen. It has not been re-characterised
end to end — the numbers above are the whole of the evidence so far — but the
"app-task stall" entry should be re-measured against this build before any
further work is aimed at it.

The reading also confirms the `paints` correction: 55 → 661 over 1218 s is 606
against 609 two-second intervals.

**Still unconfirmed:** whether the operator still sees the flash. The counters
say far less repainting is happening; only the operator can say whether the
visible symptom is gone.

**Correction to the diagnostic docs.** Line 1 is
`reset / uptime_s / paints / stall_ms / tick_catchup / stall_events`, not the
five fields recorded below. And `paints` is misleading: it is incremented once
per **diag tick**, so it counts 2 s intervals, not repaints. It cannot measure
repaint rate and should be renamed or repurposed.

## Earlier state (7 Aug 2026)

**Firmware on the watch:** `609EF4F6979A` (stamp 10:43).
**Built, not yet flashed:** `AB044776E1FE` (stamp 23:04) — circle span fill and
watchdog pets in the tile loop (N-44/N-45), plus the earlier N-36 step 1
(phase 3/8 split, band-only diag and clock repaints) and the swipe-gesture
relaxation. Several builds' worth of firmware work is queued behind one flash.
**No firmware change on 7 Aug** — the sub-app pass is companion-only, so the
queued image is still the one to flash.
**Companion:** `0.7.5-p33 (build 34)` installed; **`0.7.6-p34 (build 35)` built
and tested, NOT installed** — the Pixel was unplugged. 30 crash fix, 31
buildings, 32 settings + map retry, 33 sandbox lifetime (N-51), 34 buildings
reachable (N-52) + transient-504 backoff, 35 coastline.
**Host tests:** companion 154/154. Firmware `ctest` unchanged and not re-run —
nothing under `src/` was touched by any of this.

### Confirmed on hardware (7 Aug, 17:00)

Photographed against the OSM tile for the same spot: buildings, the coast road
and the stream all match. Also confirmed from the same log:

- **N-49 (settings) works.** `map.subscribe for slate.map r=140m` matched the
  settings screen, which is the check that was stated in advance.
- **N-52 (buildings) works.** `map: fetched 81 ways ... r=140m`, then
  `map: pushed 931 B, 50 ways` — well inside budget, nothing dropped.
- **The transient-504 backoff works.** `refetch deferred 4000ms` at 17:00:11.8,
  `map: retrying fetch` at 17:00:15.8, `fetched` at 17:00:21.4. The same failure
  cost a 30 s stall the build before.

### Coastline (build 35)

The land/sea border was missing because nothing asked for it. It is
`natural=coastline` — a different tag from `waterway`, and not covered by any
clause in the query. Worth stating plainly for anyone extending this: **the blue
sea in a standard OSM tile is not in OSM data at all.** It comes from polygons
generated separately from the coastline ways. The line is both the cheap option
and the only one an Overpass query can return.

Cheap, so it is requested at every radius unlike buildings: three ways and
16 KB around Victoria, against 181 KB for buildings. Ranked directly below the
trunk network and above every ordinary street — on a coast it is the line that
orients the whole screen.

### Map refresh intervals, as built

| | trigger | cost |
|---|---|---|
| **Redraw** (reproject cached ways) | every location fix — `LOCATION_INTERVAL_MS` = **5 s** | free, local; the push is skipped entirely when the rendered bytes are unchanged, so standing still repaints nothing |
| **Refetch** (network) | moved > **40%** of the view radius, or data older than **10 min** | one Overpass query, floored at 30 s apart |

At a 140 m radius that means a refetch after **56 m** of walking — roughly every
40 s at 1.4 m/s. Small radii refetch more often but each response is far
smaller: the fetch radius is 1.6x the view, so the queried area at 140 m is
about 12% of the area at 400 m. The two roughly cancel.

Tunables are all in `MapAdapter`: `LOCATION_INTERVAL_MS`,
`REFETCH_DISTANCE_FRACTION`, `MAX_DATA_AGE_MS`. None has been tuned against a
real walk — that is still the open question.

### N-52 — building outlines could never be fetched at any allowed setting — FIXED

Reported as "buildings are not rendered" after the radius was set below the
150 m threshold. It was not the setting and not the renderer.

`OverpassClient.fetch` widens the view radius by `BBOX_MARGIN` (1.6) so movement
inside a cached cell stays covered, then passed **that** widened value into
`buildQuery`, whose parameter was also named `radiusM` and was what
`wantsBuildings()` tested. So a 140 m view asked `224 <= 150` and got false. The
widest view that could ever have requested buildings was **150 / 1.6 = 93.75 m**
— below the 100 m minimum `examples/map/manifest.json` declares.

**The feature shipped in a state where no setting could turn it on**, and every
test passed, because the renderer tests hand `MapRenderer` data that already
contains buildings and never build a query at all. The untested seam was the one
that broke.

Fixed by moving query construction to `sdp-core/.../map/OverpassQuery.kt` with
`fetchRadiusM` and `viewRadiusM` as separate named parameters, and adding
`OverpassQueryTest` (6 tests) — including one asserting the threshold is
reachable from the radius range the manifest offers, which is the class of
mistake that made this invisible.

### "Map is busy" delays — transient 504 no longer costs 30 s

`Busy` conflated two different failures. A **429** is Overpass asking us to slow
down and deserves the full 30 s backoff; a **504/503** is its gateway timing out
under load — transient, common, and fixed by asking again shortly. Both were
treated as rate limits, so every gateway timeout became a 30 s stall on
"Map is busy".

Now split: `Busy(rateLimited = false)` retries after ~4 s, and the retry floor
in `MapAdapter` dropped from 5 s to 3 s so the client's request is not silently
stretched back out. A healthy `overpass-api.de` answers this query in about
1.4 s, and both 504s hit while capturing the test fixtures succeeded on the very
next attempt.

### N-49 — declared settings never reached a running sub-app — FIXED

**The biggest of the three, and it was not about the map.** Every sub-app that
declares settings was affected: the timer's duration, the vibrate durations,
navigation's units, location's interval, the map's radius.

`ScriptRuntimeHost.ensureRegistered` opened with `if (apps.containsKey(appId))
return`, and `settingsFor()` was only reached past that line. So the declared
settings were seeded into a sub-app's store **once per link-service lifetime**.
Changing a value and reopening the app did nothing whatsoever; only a service
restart applied it. `docs/subapp-rules.md` §5.2 promises the script that
settings are read at focus, and the host was not keeping that promise.

Caught by an operator observation that a wrong theory would have missed:
*"changing the radius does not change the displayed map at all."* The device
confirmed it exactly — `shared_prefs/slate_repo.xml` held
`setting:slate.map:radiusM = 1000` while the log read
`map.subscribe for slate.map r=400m`, 400 being the script's own default. The
stored value and the running value disagreed, which is the whole bug in one
line.

Fixed with `JsSlateAppEndpoint.seedSettings()`, called on every open including
the already-registered path. Only declared keys are overwritten, so anything
the script persisted itself (the timer's countdown) survives.
`changedSettingsReachAnAlreadyRunningApp` pins it.

**Falsifiable check on hardware:** the log line `map.subscribe for slate.map
r=…m` must now match the radius in the settings screen. If it still reads 400
after saving 140, this fix did not work.

### N-50 — the map could stall forever on a transient Overpass 504 — FIXED

Two faults compounding, from the 7 Aug log:

- `map: refetch deferred 30000ms` is the **HTTP 504** branch, not the rate
  limiter — the literal 30000 only comes from `Busy(MIN_QUERY_INTERVAL_MS)`.
  Overpass gateway-timeouts are common; two were hit while capturing the test
  fixtures.
- After that, **nothing retried for 90 s**. The location stream is the map's
  only clock, and `LOCATION_MIN_DISTANCE_M` was 5 m — so a stationary user gets
  exactly one fix, the cached one delivered at subscribe, and never another.
  No fix, no retry, "Map is busy" indefinitely.

Fixed both: the distance filter is now 0 (the 5 s interval is the throttle, and
redundant redraws were already suppressed by comparing rendered bytes), and a
failed fetch schedules its own retry rather than depending on the location
stream ticking.

### Sub-apps are now launcher-only (7 Aug)

The per-sub-app buttons on the companion's main screen are gone, along with
`CompositorHost.openTimer/openNavigation/openCamera`, the three
`ACTION_OPEN_*` intents and their static helpers. One method per sub-app meant
a code change was needed before a newly installed one could be opened at all,
which is backwards for downloaded apps.

`Open TestApp` and `Open Notifications` **stay**: they are Kotlin apps, are not
in `InstalledStore`, and so cannot appear in the watch launcher at all.

The camera runtime permission used to hang off the removed "Open Camera"
button. It has moved into the grant button, now **"1c. Grant sub-app
permissions (location, camera)"**, which asks only for what is still missing.

**Note for the operator:** five packages are currently hidden from the launcher
in `shared_prefs/slate_repo.xml` — `slate.vibrate`, `slate.camera`,
`slate.image`, `slate.image.vector`, `slate.navigation`. That is the opt-out
checkbox in the repository screen, not a defect, but with the main-menu buttons
gone those apps are now unreachable until they are re-enabled there.

### N-51 — a dropped sandbox reference bricks JS for the whole process — FIXED (recurrence 10 Aug)

The tail of N-48, and the deeper cause. After N-48 the companion no longer
died, but Local Map then reported **"Did not start"** with
`JS sandbox unavailable: Binding to already bound service` — thrown from the
*first* call, meaning no sandbox reference was held and yet the service was
bound.

**Recurrence (post-OTA, 10 Aug):** same user-visible brick —
`JS sandbox is bound but unreachable — Force-stop Slate and reopen it` — after
an OTA reconnect while the FGS stayed alive. Root class unchanged: androidx
permits one bind per process; losing the Java handle without `close()` shuts
the static gate forever. Extra failure modes seen in the field:

- Seed at service start failed and was only logged — every later launch stayed
  bricked until force-stop.
- `CompositorHost.stop()` closed isolates but not the shared sandbox, so a
  sticky FGS restart left the gate shut with no handle.
- Launcher swipe while GATT was up but HELLO not Ready pushed nothing
  (`pushToWatch` drops) — looked like a dead gesture until disconnect/reconnect.

**Mitigations shipped in companion 0.8.2-p40 / versionCode 41:**

| | |
|---|---|
| Strong `sandboxInstance` + `forceReset()` / `releaseSharedSandbox()` always `close()` | `AndroidJsEngine.kt` |
| `create()` retries once after forceReset on brick-shaped errors | `AndroidJsEngine.kt` |
| Seed failure and launcher start both reset+retry | `CompositorHost.kt` |
| `stop()` calls `forceReset()` after `scripts.close()` | `CompositorHost.kt` |
| Defer launcher open until session Ready; flush on Ready edge | `CompositorHost.kt` |

Read out of the androidx 1.0.0 sources rather than guessed at, after two
theories had already been spent on this area:

- `JavaScriptSandbox.bindToServiceWithCallback` gates on a private static
  `sIsReadyToConnect.compareAndSet(true, false)` and throws
  `IllegalStateException("Binding to already bound service")` when it is
  already false.
- `killImmediatelyOnThread()` — the sandbox-death path — sets `DEAD` and
  unbinds but **does not reset the flag**.
- `unbindService()`'s own javadoc: *"This will not, by itself, make JSE ready
  to create a new sandbox. The JavaScriptSandbox object must still be
  explicitly closed."*
- `close()` is the **only** thing that resets it. There is no static recovery.

So any path that loses the reference without closing bricks JS for the life of
the process. Ours did: `awaitFuture` suspends on a
`suspendCancellableCoroutine`, and `create()` ran in the caller's coroutine. A
caller cancelled after the bind completed left the future's listener resuming a
dead continuation — sandbox created, reference garbage, flag stuck false. Also
note androidx's own cancellation listener unbinds **without** resetting the
flag, so cancelling the bind is equally fatal; `awaitFuture` deliberately does
not propagate cancellation, and there is a comment saying so.

**Fixed** by moving creation into a `Deferred` owned by a private
`sandboxScope`. A cancelled caller now cancels only its own `await()`; the
Deferred keeps the reference, so `close()` is always reachable. Every failing
path either closes or never bound.

**An honest note on N-48:** the crash guard made this *worse* before it made it
better. The process used to die and take the poisoned static with it; once it
stopped dying, the bricked state persisted until a force-stop. That is why the
error message now names the recovery, and why installing build 33 force-stopped
the app first.

**If still bricked after install:** force-stop once to clear the static gate,
then reopen — subsequent OTA/reconnects should self-heal via forceReset.

### N-48 — the companion crash on opening a sub-app (7 Aug) — FIXED

**Not the map.** Opening Local Map was the trigger, not the cause: it is the
sub-app most likely to be opened when memory is tight, and the fault is on the
JS sandbox path shared by every sub-app.

Crash log, `08-07 10:51:52.934`:

```
FATAL EXCEPTION: main
java.lang.IllegalStateException: Binding to already bound service
  at androidx.javascriptengine.JavaScriptSandbox...
  at slate.app.script.AndroidJsEngine$Companion.sharedSandbox(AndroidJsEngine.kt:104)
  at slate.app.script.AndroidJsEngine$Companion$create$2.invokeSuspend(AndroidJsEngine.kt:129)
```

Line 129 is the **recovery** path, and it could never succeed:

1. `createIsolate()` throws `IllegalStateException` — the sandbox host process
   had been reclaimed, leaving a stale handle. Expected, and recoverable.
2. The recovery set `shared = null` and rebound — but **never called
   `close()`**. Dropping the reference does not release the service binding.
3. androidx therefore refused the rebind with
   `IllegalStateException("Binding to already bound service")`.
4. Nothing caught it. An uncaught exception in a coroutine reaches the thread's
   default handler, which on Android kills the process.

So the "recovery" reliably converted a recoverable fault into process death.
This is the second half of **N-38**, which was closed on 6 Aug when the sandbox
became a process-wide singleton; the singleton was right, its recovery path was
not.

**Both halves fixed:**

| | |
|---|---|
| `releaseSharedSandbox()` closes the old sandbox before clearing it, so the rebind can actually bind | `AndroidJsEngine.kt` |
| A failed rebind throws `ScriptEngineException` instead of a raw `IllegalStateException` | `AndroidJsEngine.kt` |
| `launchFromLauncher` catches registration failure, logs it, and **draws "Did not start" on the watch** rather than the tap doing nothing | `CompositorHost.kt` |
| The link service's scope has a `CoroutineExceptionHandler` — a `SupervisorJob` alone does nothing about an *uncaught* exception | `LinkForegroundService.kt` |

That last one is the general lesson: every sub-app launch, adapter callback and
compositor push runs in the link service's scope, so **any** of them could kill
the BLE link. Now they cost a log line.

**The follow-on symptom — "after restarting, the launcher cannot be opened
until a reconnect" — is not separately diagnosed.** The restart log shows a
double GATT bring-up (two `onServicesDiscovered`, `writeDescriptor rc=201`),
which is N-24's signature. It is most likely a *consequence* of dying
mid-session rather than an independent defect, so the honest next step is: see
whether it still happens now the crash is gone. If it does, capture the log
from the swipe that fails to open the launcher — the current capture starts
after the crash and shows only the restart.

### `slate.map` — north-up OSM vector map (7 Aug)

Built and installed; **never seen on hardware**. Open **Local Map** from the
watch launcher after granting location.

The sub-app is a *thin controller* — the Camera precedent. It cannot do this
work itself: `slate.http` is a stub, and projecting and simplifying a few
hundred ways would blow the 500 ms eval deadline. So the host fetches OSM data,
projects it, renders a display list and pushes it under the app's focus with
`pushHostDisplayList`. The script draws only the screens that exist when there
is no map — 70 B worst case, measured.

| Piece | Where |
|---|---|
| `slate.map.subscribe / unsubscribe` — **no refresh command, by design** | `shared-js/slate_host.js` |
| Overpass fetch, grid snapping, rate limit, cache | `app/.../map/OverpassClient.kt` |
| Response parsing (in sdp-core so it is desktop-testable) | `sdp-core/.../map/OverpassParser.kt` |
| North-up projection + Cohen–Sutherland clipping | `sdp-core/.../map/MapProjection.kt` |
| Douglas–Peucker in screen space | `sdp-core/.../map/MapSimplify.kt` |
| Budget-driven display-list emission | `sdp-core/.../map/MapRenderer.kt` |
| Refresh policy — reproject vs refetch | `app/.../map/MapAdapter.kt` |
| Sub-app (`refreshPolicy: manual`) | `companion/examples/map` — **Local Map** |
| Tests (20) + real 190 KB Overpass fixture | `sdp-tests/.../map/` |

**The refresh is the companion's, structurally.** `slate.map` exposes only
subscribe and unsubscribe, so there is no command a sub-app could poll with.
Two tiers, because the prices differ by orders of magnitude:

- **Reproject** — free and local, on every location fix. Cached ways are
  redrawn around the new position, so the map tracks the user continuously.
- **Refetch** — network, rate-limited, only when the user leaves the area the
  cached data covers (40% of the radius) or it ages past 10 minutes.

The location stream is the clock; there is no separate timer. Standing still
renders byte-identical output, and `MapAdapter` compares against the last push
and suppresses it — otherwise the watch would take a full-screen repaint every
5 s to change nothing, straight into N-36.

**The budget is the design.** 2048 B against unbounded source detail means
something is always dropped; the only question is whether it is chosen. Ways
are emitted most-important-first (motorway → trunk → … → footpath) and the loop
stops when the next would breach the cap. Real data around Victoria: **190 KB
of Overpass JSON in, 217–2030 B out** depending on radius.

**North is up structurally, not by setting.** There is no rotation term
anywhere in `MapProjection`, and `northIsUp()` asserts it — an inverted map
would otherwise pass every other test.

#### Two things found on the way

| What | Status |
|---|---|
| **The desktop interpreter never rendered `TEXT_SCALED`.** Legal — 0xE0-0xEF is the extension range old implementations are *meant* to skip — but every sub-app now draws its text that way, so every preview PNG and golden image of a modern screen silently showed no text at all | **Fixed.** The Kotlin parser decodes it and skips to the declared payload length regardless, so the forward-compat contract is unchanged for every other extension |
| **Overpass JSON was parsed on the main thread.** `fetch()` wrapped only the network call in `Dispatchers.IO`; the parse ran on the caller's dispatcher, which is `Dispatchers.Main.immediate` — 50 ms on the desktop for 190 KB, so a phone-scale multiple of that on the thread of the foreground service that owns the BLE link | **Fixed** — parse moved inside the IO context. Found by measuring rather than reading (`:sdp-tests:mapTiming`) |
| `HostCapabilities.ALL` still had no `slate.phone` token, so `examples/vibrate` could not declare its main dependency in `requires` | **Fixed** — added alongside `slate.map` |

Reproject-and-render itself is **0.17–0.27 ms median** on the desktop, so
running it on the location callback thread once per fix is fine even at a
generous phone multiplier. Measured, not assumed — `:sdp-tests:mapTiming`.

#### Seeing it without hardware

```bash
cd companion && ./gradlew.bat :sdp-tests:mapPreview --offline
```

Renders the map to PNGs in `sdp-tests/build/map-preview` through the real
pipeline — real captured OSM data, the real renderer, the real display-list
interpreter. The watch is sealed, so without this every map tweak costs a
build, an install and a photograph before anyone can judge it.

#### Building outlines (7 Aug)

Added, and **only fetched at a view radius of 150 m or less**. That is a
bandwidth and legibility decision, taken from measurements rather than taste:

| | roads + water + rail | buildings alone |
|---|---|---|
| ways within 700 m of Victoria | 269 | **1575** |
| decompressed | 190 KB | **1260 KB** |
| on the wire (gzip) | 31 KB | **181 KB** |
| display-list bytes wanted | ~1.2 KB | **~28 KB** |

Buildings want roughly fourteen times the entire 2048 B budget and six times
the data of everything else combined. The share the budget has to discard, by
radius: **0% at 100 m, 3.5% at 150 m, 29% at 175 m, 43% at 200 m, 60% at
250 m.** Past the knee the map reads as arbitrary rather than sparse — a gap
where a building was dropped looks like open ground, which is worse than
drawing none.

So they rank last (`MapClass.Building`), which means the existing budget loop
needed no special case: at a radius where they do not fit they are simply the
first thing dropped. And `OverpassClient` does not ask for them above the
threshold at all, so the data is never spent on outlines that would be
discarded.

#### What the map actually costs in data

Measured, since "someone else's bandwidth" deserved a number rather than a
worry. One response is **190 KB raw but 31 KB gzipped** — Overpass serves gzip
and `HttpURLConnection` requests and decompresses it transparently.

| radius | refetch after | walking | queries/h | data/h |
|---|---|---|---|---|
| 200 m | 80 m | 57 s | 63 | 0.4 MB |
| 400 m | 160 m | 114 s | 32 | 0.8 MB |
| 800 m | 320 m | 229 s | 16 | 1.7 MB |
| 2000 m | 800 m | 571 s | 6 | 2.4 MB |

Standing still is **zero** — the snapped cell hits the cache and no request is
made. Worst case the 30 s floor allows is 3.8 MB/h.

**So the user's data plan is not the issue**: sub-1 MB per hour of continuous
walking at the default radius is less than a couple of web pages. The earlier
note was about Overpass's *server* load, and it conflated the two. Overpass
rate-limits by query count and CPU time, not bandwidth, because each query is a
spatial search over a planet-scale database — 32 queries an hour from one watch
is nothing, but it is the metric that would matter at scale. Restated
precisely: **one watch is free to do this; a shipped product should not point
thousands of them at a donated public endpoint.** The client identifies itself,
floors queries at 30 s, and snaps the bounding box to a grid so what leaves the
phone is a cell the user is somewhere inside rather than their exact fix.

#### What still needs the operator

- **Whether it renders at all**, and whether roads are legible at 240x240. The
  previews say yes; the panel is the authority.
- **Whether the refresh cadence feels right** while walking. `MapAdapter`'s
  constants are reasoned, not tuned against a real walk.
- A 2030 B screen at 800 m radius sits close to the credit window (§3), so that
  radius is the one most likely to drop intermittently — worth watching before
  the smaller ones.

### `slate.location` — the phone's position, for any sub-app (7 Aug)

Built and installed; **no fix has been observed on hardware yet**. To try it:

1. Tap **1c. Grant location (for sub-apps)** on the companion's main screen.
   The runtime permission is still ungranted on the Pixel — checked on device,
   it is in the never-requested state.
2. Start the link (or open **Sub-app repository**). Either one runs the bundled
   seeder, which is what installs **Where Am I**. Verified on device that
   launching `MainActivity` alone does **not** — seeding hangs off
   `CompositorHost.start()` and `RepoActivity`, not the main screen, so the
   demo will not appear in the launcher until one of those has run.
3. Open **Where Am I** from the watch launcher.

Confirmed on device while checking this: the 1.1.0 reinstalls from the
conformance pass **did** land — `timer`, `vibrate`, `navigation` and `camera`
all read 1.1.0 in `files/repo/subapps`. The version-bump reinstall path works.

| Piece | Where |
|---|---|
| `slate.location.subscribe / request / unsubscribe` | `shared-js/slate_host.js` |
| `LocationAdapter` — raw `LocationManager`, no Play Services | `app/.../location/LocationAdapter.kt` |
| Host routing, one subscriber at a time | `CompositorHost.handleLocationAdapter` |
| Runtime permission + FGS type | `AndroidManifest.xml`, `LinkForegroundService.foregroundTypes()` |
| Grant button | `MainActivity` — "1c. Grant location (for sub-apps)" |
| Demo sub-app (157 B, 8 drawing ops, measured) | `companion/examples/location` — **Where Am I** |
| Contract tests (6) | `sdp-tests/.../LocationBindingTest.kt` |

The permission half already existed — `ScriptPermission.Location`, the gate in
`BindingSurface`, the `slate.location` capability token, and the
`THIRD_PARTY_BLOCKED` entry that makes a downloaded app ask the user. What was
missing was the binding, the adapter and the Android permission. `location`
stays third-party-blocked: a bundled demo gets it at Official trust, a
sideloaded app needs an explicit grant in the repository screen.

**One structural change was needed.** `Compositor`'s `onAdapterCommand` did not
pass the app id, so the host could not tell which sub-app issued a command.
`nav` and `camera` work around that by hardcoding a single app id each, which
is why each serves exactly one sub-app. The callback now carries the id
(`reg` was already in scope), so location serves any app that asks. Nav and
camera were left on their hardcoded ids — changing them is a separate job.

**Two things that could have broken the link service, and how they are handled:**

- From Android 14, `startForeground` with a type the app lacks permission for
  throws `SecurityException`, and the existing catch calls `stopSelf()`.
  Claiming the `location` FGS type unconditionally would therefore have killed
  the **entire BLE link service** on any phone where the user never granted
  location. The type is now computed at start from the actual grant.
- The FGS type is fixed when `startForeground` runs, so a grant arriving later
  buys nothing until it is re-asserted. `refreshForegroundServiceType()` does
  that and the grant button calls it. Without the `location` type, fixes stop
  as soon as the app is backgrounded — the normal state for a watch companion.

`ACCESS_BACKGROUND_LOCATION` is deliberately **not** requested. A sub-app only
draws while its screen is on the watch, so there is no case for it, and it
costs a separate settings trip.

Still unverified, and only the operator can settle it: whether a fix actually
arrives, whether it keeps arriving with the app backgrounded, and whether the
approximate-only grant behaves as the code assumes (either grant is treated as
sufficient, and the adapter does not assume the accuracy it got).

### Sub-app conformance pass (7 Aug) — built, unverified on hardware

Every app in `companion/examples/` now conforms to `docs/subapp-rules.md`:
§4 header, bounded loops, declared permissions matching actual use, BACK
handled, and a **measured** display-list size.

Sizes are wire bytes, measured by running each app's real lifecycle against the
real `shared-js` builder and decoding what it emitted — not estimated:

| App | Bytes (worst case) | Drawing ops | Verdict |
|---|---|---|---|
| `timer` | 63 | 4 | Reference. Unchanged but for the budget line (it said "~60 B") |
| `vibrate` | 143 | 8 | Conforming header; `shortMs` / `longMs` settings |
| `camera` | 115 | 6-7 | Conforming header; text moved to font 1 scaled |
| `navigation` | 122 | 6-7 | Conforming header; text moved to font 1 scaled; `units` setting |
| `image` | **3987** | 2 | **Over the 2048 B practical limit.** Reported, not changed |
| `image-vector` | 174 | 24 | Cheapest on the wire, most expensive to render |

**`image` is the one that still breaks a limit**, and deliberately so — it is
the operator's artwork. The largest square that fits the practical limit is
**45x45 = 2043 B** (46x46 is 2134 B and misses); that is `W`/`H` in
`gen_logo.py` and a regenerated array. It has not been changed. The over-budget
note, the number, and the proposal are now in the app's header and README.

**`image-vector` was the app that reset the watch (N-44/N-45), and its display
list is 174 B.** Worth stating plainly because it inverts the intuition: bytes
and render cost are independent. It is cheap to send and expensive to draw —
23 filled discs, 7 of them r >= 78, walked once per tile, 30 times. `image` is
the mirror image: one PATCH is nothing to draw and 3987 B to deliver. §2 caps
bytes, §2.1 caps ops, and an app can pass one while failing the other.

#### Found while doing it

| What | Status |
|---|---|
| **The Kotlin/JS golden test for the timer face had been failing since 5 Aug.** `JsUiScenes.timerFace` still built with `text` after the app moved to `textScaled`, so `timerApp_focusRenderInputPersist` was red and the byte-identical rule from `CLAUDE.md` was unenforced for that face | **Fixed.** Both the scene and the op-by-op parity test now use `textScaled`. Companion suite went 111/112 → 117/117 |
| **`gen_logo.py` rewrites the whole of `image/main.js`, header included.** A conformant header there would have been silently reverted by the next `python gen_logo.py` | **Fixed.** The template now carries the §4 header, and the budget figures are computed from `W`/`H` rather than typed, so they stay true if the size changes. `main.js` is now byte-for-byte what the generator emits |
| `gen_logo.py` wrote `encoding="ascii"`, which the §-citing header would have crashed on | **Fixed** — UTF-8, matching every other sub-app source and both `entryJs()` and `ScriptResources.read()` |
| **`BundledPackageSeeder.DEMOS` carries a hard-coded permission set per demo** that must track each manifest by hand. Adding `storage` to two apps meant editing it too | **Fixed for now** and commented. Not enforced by a test — the seeder is in the `app` module and `sdp-tests` is JVM-only |
| **There is no `slate.phone` token in `HostCapabilities.ALL`**, so `vibrate` cannot declare its most important dependency in `requires`. It declares none rather than a misleadingly partial list | **Open, host-side.** One line to add; not done, as it is outside a sub-app pass |
| Font 0 (3x5) does carry the full printable ASCII set — 95 glyphs, 32..126, confirmed in `shared/fonts/font0_3x5.json` and `include/font_builtin.hpp`. The "no letters, codepoints 45-58" note in `status.md` describes it **before** the 6 Aug regeneration | No action — but `camera` and `navigation` drew everything at 3x5 scale 1, which is legible only in the sense that the glyphs exist. Both now use font 1 scaled, as `timer` and `vibrate` already did |

Five host tests were added (`BundledSubAppConformanceTest`) covering the
mechanical half of the §6 checklist: every bundled manifest parses, settings
imply the storage permission, declared defaults survive `sanitise`, entry
scripts load, and **every bundled app focuses under Rhino and relinquishes on
BACK** with its list asserted under 2048 B. The store is left empty for that
last one on purpose, so each app runs its §5.2 missing-value path.

#### What still needs the operator

Nothing here has been on hardware. Specifically unverified:

- **`camera` and `navigation` at font 1.** Both were laid out for a 3x5 cell
  and are now 5x7 scaled. The arithmetic says everything fits inside 240 px
  (widest line is `navigation`'s 18-char street at 216 px), but "fits" and
  "looks right" are different claims and only the operator can make the second.
- ~~**`vibrate` and `navigation` reinstalling.**~~ **Confirmed 7 Aug** — all
  four bundled demos read 1.1.0 in `files/repo/subapps` on the Pixel.
- **The two settings screens**, which have only ever been exercised by `timer`.
- `image` / `image-vector` need re-sideloading — `slate.image.zip` and
  `slate.image.vector.zip` were repacked, since their `main.js` changed.

### The launcher shipped and works (6 Aug)

Swipe right-to-left opens an app drawer listing the installed JS sub-apps;
tapping a row focuses that sub-app; swipe left-to-right closes it. Confirmed on
hardware: `Launcher: tap row 3 -> slate.vibrate`, then `phone.vibrate: 150ms`.

A fourth sub-app — **Buzz Phone** — exercises a binding whose whole effect is on
the handset (`slate.phone.vibrate`, gated on a new `ScriptPermission.Vibrate`).
Per-app launcher visibility is a checkbox in the repository screen, stored as an
opt-OUT set so newly installed apps appear by default.

Also new: **built-in font 1 (5x7)**, compiled into flash as a second table
rather than shipped through the asset pack. The 3x5 had no letters at all
(codepoints 45-58), which is why every version line has read as boxes. The
asset-pack route was measured and rejected — `GlyphCache` carries a 6 KB blob
against 448 B of RAM headroom and has never been instantiated, and the asset
transfer path has never run on hardware. Compiled-in costs 665 B of flash and
zero RAM. Both fonts generate from ASCII-art sources under `tools/codegen/`.

### Sub-app rules now exist — `docs/subapp-rules.md`

Normative, and `CLAUDE.md` requires reading it before touching a sub-app. It
carries the render budgets, the required comment header, and the settings
schema. Written after `examples/image-vector` **reset the watch**.

Sub-apps may now declare `settings` in their manifest; the companion shows a
gear beside the entry in the repository list and generates the screen. Values
are seeded into the sub-app's store, so scripts read them with
`slate.store.get(key)` and need no new binding. `examples/timer` declares
`durationSec` as the reference case.

### Defects found and fixed on 6 Aug (evening)

| ID | What | Why it mattered |
|---|---|---|
| **N-44** | Filled circles were drawn as `rad+1` concentric Bresenham outlines — ~58,000 points for one r=120 disc, replayed once per tile (30x) | `image-vector` took the app task past the ~7 s bootloader watchdog and **reset the watch**. Now scanline spans, O(r) per disc |
| **N-45** | The renderer never petted the watchdog, so any sufficiently expensive display list could reset the watch | A sub-app must not be able to do that. Pets now run inside the bounded 30-iteration tile loop, through `pet_service()` so a held button still starves the dog. Contract recorded in `CLAUDE.md` |
| **N-46** | The display credit window only ever decremented: the watch advertises 4096 free at depth 0 and **0** while a screen is up, and the host ignored the zero | Large screens failed intermittently and unpredictably — a 3987 B app worked or not depending on how many pushes had happened since the last return to the watch face. Screens now release their credit when they leave the watch |
| **N-47** | `maybePush` had three silent `return false` paths | A sub-app that focused and then showed nothing was indistinguishable from one that never ran. Drops now name the reason and the byte counts |
| — | Bundled demos were never refreshed once installed | A manifest edit (adding settings) never reached a device that already had the app. The seeder now reinstalls when the bundled version differs |

### Defects found and fixed on 6 Aug

Five were mine, introduced the same day. Recorded because the pattern matters
more than the individual bugs: **four of the five were invisible until they hit
hardware, and three of those were silent** — no log line, no counter.

| ID | What | Why it mattered |
|---|---|---|
| **N-37** | `check_rect()` bounded every op to the 240 px display with no scroll-region awareness | `SCROLL_REGION` and the zero-RTT local scroll had **never been usable for their purpose** — content taller than the viewport is the only case where scrolling means anything. Nothing had exercised it until the launcher |
| **N-38** | `AndroidJsEngine.create()` built a new `JavaScriptSandbox` per engine; androidx allows **one per process** | A second JS sub-app, or a sticky service restart, threw `IllegalStateException: Binding to already bound service` on the main thread and **killed the companion**. Three crashes in one six-minute session |
| **N-39** | `onLinkLost()` never cleared the compositor stack | The host kept believing a sub-app owned a screen the watch had already dropped. With N-38's restarts this wedged the launcher gesture until the app was restarted |
| **N-40** | `RepoManager.refreshLocal()` only built from `InstalledStore` when the catalogue was empty | Any installed package no index listed was invisible on **both** repository tabs — including the Installed tab, which filters that same catalogue. Buzz Phone was the first package to hit it |
| — | Hit rects recorded in content space, not screen space, and not rebuilt on scroll | A scrolled row's tap target stayed where the row used to be |
| — | Gesture latch required `touching` for slides (InfiniTime's rule) | At Slate's poll latency the finger has usually lifted: 1018 interrupts produced 24 events. Now a **documented divergence** — the rule is right at InfiniTime's timing and wrong at ours (see N-36) |

### Still failing

`ctest -R ble_link` — `drop/reject: got 0 want 1`. Unchanged, still not
investigated, still on the `sdp_frame.cpp` reassembler path. The suite is 18/18
**only with `-E ble_link`**; do not read "all tests pass" as covering it.

### N-31 CLOSED and P-1 CLOSED (5 Aug, 21:29) — verified on hardware

Line 3 `0.16/31.0.0.3/244.0.8.6`: **244 touch interrupts, 0 read failures**,
8 touches decoded, 6 hits. Companion log shows three `notify len=7` input
events, each followed by a state-change push. The `timer` JS sub-app counted
down and paused on tap.

That satisfies P-1's done-condition — *"a display list composed on the phone is
visibly rendered on the watch, a tap on it is received by the phone"* — and it
does so against a **JS sub-app**, which is what this file insisted on. `dl_ok`
31, frame drops 0, parse 0 ms, render 222 ms (max since boot, dominated by the
local face rather than the sub-app list).

### N-31 root cause (5 Aug, 21:00) — the TWIM SHORTS constants

The TWIM `SHORTS` bit positions in `nrf52832_regs.hpp` were shifted against
the nRF52832 PS and against Nordic's own `nrf52_bitfields.h`, vendored in
`third_party/nrfx/mdk/`: `LASTTX_STOP` was 8 (that bit is LASTTX_SUSPEND) and
`LASTTX_STARTRX` was 10 (that bit is LASTRX_STARTTX).

Consequence: `twi::write` suspended instead of stopping, and `twi::write_read`
never issued the repeated START. Neither raised `EVENTS_STOPPED`, so both died
on their timeout — **every I2C transfer since bring-up, on every device**.
Plain `twi::read` had the one correct short, and nothing uses it. Fixed in
`16D04B4EF949`.

**The wrong turn, kept on the record.** The previous entry here blamed the
SCL/SDA pin drive (`S0S1` where InfiniTime uses open-drain `S0D1`). That is a
genuine divergence and is fixed — but `F4A879DE6880` shipped with the fix and
measured `5.5.0.0`, unchanged. Three separate theories were tried against this
symptom (bus wake, full re-init, pin drive) because the diagnostic counted
failures without recording their cause. Diag line 3's touch group now ends
with the `twi::Status` of the last read; that one field distinguishes all
three classes of fault without a flash cycle.

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

**Still open:** taps (N-31 — superseded by the entry at the top of this file:
the IRQ *does* reach the driver, one per tap; it was the I2C read underneath
that never completed), and a long-run degradation where the watch stops
replying entirely until reconnect (see below).

### The sub-app stack — what actually exists

| App | Kind | Priority | Notes |
|---|---|---|---|
| `ClockApp` | Kotlin | **AMBIENT** | Ambient watch face. Owner wants it gone; see below |
| `NotificationsApp` | Kotlin | NORMAL, raised at **INTERRUPT** | Raised by an incoming notification (`maybeInterrupt`), so it can pre-empt any screen |
| `TestApp` | Kotlin | NORMAL | The P-1 reference app |
| `LauncherApp` | Kotlin | NORMAL | The app drawer (6 Aug). Reserved swipe-left opens it; swipe-right closes it. Lists only what `InstalledStore` holds, which is JS-only by construction |
| `timer`, `camera`, `navigation`, `vibrate`, `location`, `map` | **JS sub-apps** | NORMAL | Bundled in `companion/examples/` and seeded. Timer ≠ clock. Nav/camera/location/map are **functional** thin controllers (host adapters). `vibrate` demos phone haptic. Sideload-only: `image`, `image-vector` |

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
| **N-31** | Touch dead. **Cause:** TWIM `SHORTS` bit positions were shifted in `nrf52832_regs.hpp`, so writes suspended instead of stopping and write_read never issued the repeated START — every I2C transfer timed out, on every device. Two earlier theories were wrong: `buses_idle()` (ENABLE=0 preserves PSEL/FREQUENCY/PIN_CNF) and the `S0S1` pin drive (fixed, but changed nothing) | **CLOSED** 5 Aug — `244.0` irq/readfail on hardware |
| **N-42** | ~~Watch resets in normal use~~ — **WITHDRAWN 6 Aug: it was not happening.** Reset `6` was seen once in two days; every other reading is `4` (SREQ alone = the flash itself), including a 1526 s uptime. The operator reboots the watch by hand during testing, and that is sufficient on its own: `poll_reboot_button()` resets via SREQ after an 8 s hold while `wdt::pet()` deliberately withholds pets for the whole hold, so the ~7 s bootloader dog fires during it too. DOG plus SREQ is `6`. `RESETREAS` accumulates until the app clears it, so one value can cover two intended resets — an OTA swap does the same thing. **Watch for:** a `2` or `6` at a boot that followed neither an update nor a button hold | Closed — filed off a single reading without checking the others |
| **N-43** | `status=8` supervision timeouts drop the link mid-session with no watch reset — uptime keeps climbing straight through them | Open — plausibly N-36: a ~1 s app stall can outlast the connection supervision window |
| **N-41** | OTA needed **ten** resyncs (`OTA timeout ... resync #9`, `#10`), ~5 s each, ending in a `status=8` disconnect. The image still installed and confirmed | Open — same stall family as N-36; worsens as images grow |
| **N-40** | Repository screen could not show an installed package absent from any index | **Fixed** 6 Aug (`refreshLocal` unions the store) |
| **N-39** | Compositor stack survived link loss, so the host believed a dead screen was live | **Fixed** 6 Aug (`resetStack()` on link loss) |
| **N-38** | A second `JavaScriptSandbox` per process crashed the companion | **Fixed** 6 Aug (process-wide singleton; per-sub-app isolation still via isolates) |
| **N-37** | Scroll regions could not carry content taller than the viewport | **Fixed** 6 Aug (parser tracks the region, bounds against `contentH`; three host tests pin the boundary) |
| **N-36** | App-task stall 887-1002 ms with 8-16 inbox drops. Worst phase is 3 (session/core tick, 444-477 ms), render 222-238 ms — the **face repaint**, not vibration | Open — **largest remaining defect and the recommended next work** |
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

### N-36 — The app-task stall (recommended next work)

- **Status:** Open. The highest-value item on this list.
- **Measured repeatedly:** `stall` 887-1002 ms on diag line 1; worst phase **3**
  (session/core tick) at 444-477 ms; `render` 222-238 ms; `inbox_drop` 8-16 per
  session.
- **What it already costs**, all measured rather than assumed:
  - Touch is read hundreds of ms late, which forced a deliberate divergence
    from InfiniTime's gesture rule. 1018 interrupts produced 24 events.
  - Ten OTA resyncs in one transfer (N-41), ~5 s each.
  - Inbox drops every session, and very likely N-35 in its entirety.
- **The obvious first lever, untried:** the diagnostic overlay forces a
  **full-face repaint every 2 s**, and a full repaint is ~238 ms of SPI —
  roughly a 12 % duty cycle spent redrawing 240x240 to update three lines of
  text. `build_ota_banner` already demonstrates the fix in this codebase: it is
  band-only, ~5 tile passes instead of 30, precisely so it can run mid-transfer.
- **First measurement to take:** split phase 3. It currently times
  `session.tick` and `core.tick` together, and `core.tick` contains
  `show_current()`. Which of the two owns the 477 ms decides whether this is a
  rendering problem or a session-logic problem — the same "one reading splits
  the problem in two" that closed N-31.

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
line 1: reset/uptime/paints/stall/tickCatchup
line 2: phase.ms/mV/parse.render
line 3: frameDrop.inboxDrop/applied.rejected.dropped.sessState/irq.readfail.touch.hit.twiStatus
```

Corrected 5 Aug against `local_ui.cpp`: line 1 no longer carries the raw
button level or the BLE stage/rc (both dropped when the SDP counters were
added), and the touch group on line 3 is now **five** fields, not two — IRQ
latches, read failures, touch events, hits, and (added 5 Aug) the
`twi::Status` of the last read: `0` Ok, `1` Timeout, `2` AddrNack,
`3` DataNack, `4` BusError. `9.9.0.0` therefore read as "nine interrupts, nine
failed reads, nothing decoded" — with no way to tell why they failed, which is
what the fifth field is for.

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

**Revised 6 Aug.** P-1 is closed and the launcher is built on top of it, so the
old ordering is spent.

1. **N-36 — the app-task stall.** Everything below is cheaper once it is fixed,
   and several open items are probably symptoms of it. Detail in part I.
2. **N-41 / N-35 / N-43** — the link cluster (OTA resyncs, watch stops
   replying, supervision-timeout drops). Expect N-36 to move all three;
   re-measure before treating them as separate work. N-42 was withdrawn.
3. **ble_link host-test failure** — small, and it sits on the one path
   (`sdp_frame.cpp`) nobody has examined.
4. **I-15** — the last remaining way to strand a user with no explanation and
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
