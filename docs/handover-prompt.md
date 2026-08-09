# Handover prompt — paste this to the next instance

You are picking up work on **Slate** (repo: `C:\Users\highj\Documents\Projects\EvoTime`),
a thin-client smartwatch OS for the PineTime: nRF52832 firmware in C++17 plus an
Android companion in Kotlin. The watch is **sealed — no SWD**. The only way to
see what it is doing is the on-screen diagnostic overlay and the companion's
in-app log, both of which the operator photographs and pastes to you.

Written 9 August 2026, 14:00. Replaces the 6 August version.

## Read these first, in this order

1. `CLAUDE.md` — standing rules. The hardware pin map is authoritative; do not guess it.
2. `docs/issue-prompts-open.md` — **single point of truth** for open work and
   current state. `issues.md` and `status.md` are historical; where they
   disagree, that file wins. Its newest section is at the top.
3. `docs/subapp-rules.md` — normative for JS sub-apps. Read before touching one.
4. `docs/infinitime-parity.md` — what mirrors InfiniTime, what deliberately does not.

Reference tree for low-level parity: `C:\Users\highj\Documents\Projects\InfiniTime-main`.
**Read it before debugging any driver.** Every driver defect this project has
had was a divergence from it, including both found on 9 August.

## State as of 9 August 2026, 14:00

| | |
|---|---|
| **Firmware built, awaiting flash** | `5543D0BF9804` (stamp `13:47`) — `build/dfu/slate-dfu.zip` |
| **Last firmware the operator flashed** | `1560AB3C3466` (stamp `12:34`) |
| **Companion installed on the Pixel** | build 38, `0.8.2-p37` |
| **Host tests** | **22/22 with `-E ble_link`**; `ble_link` genuinely fails, see below |
| **Companion tests** | 168/168 (`:sdp-tests`) |
| **RAM link slack** | 200 bytes. See I-19 — this is thin and known |

**The repo is a long way behind the working tree.** `HEAD` is `66c5c1c "01 080826"`;
everything from 8–9 August is uncommitted, including seven new files. Do not
`stash`, `reset` or `checkout` source files to get a baseline — you will destroy
work. The operator has not asked for a commit; do not make one unprompted.

Uncommitted new files: `src/settings_sync.cpp`, `include/settings_sync.hpp`,
`tests/host/test_settings_sync.cpp`, `tests/host/test_bma42x.cpp`,
`companion/.../slate/session/WatchSettings.kt`,
`companion/.../slate/session/WatchSettingsTest.kt`,
`companion/app/src/main/java/slate/app/settings/`.

### Working, confirmed on hardware

Taps and the app launcher; JS sub-apps; the OSM vector map (buildings and
coastline); OTA; display sleep and the 20 s timeout; **wake on both charger
edges**; the **watch settings screen** (swipe left-to-right from the face — three
tappable rows); the **companion settings screen** and the bidirectional sync.

### Verified broken as of the last flash, fix built but NOT confirmed

**Step counter reads 0** and **raise-to-wake never fires**. Both were traced on
9 August to the accelerometer driver — see N-59 and N-60 in
`docs/issue-prompts-open.md`. Both fixes are in `5543D0BF9804`.

**This is the first thing to check.** Ask the operator to flash it, then:

- Steps: walk a hundred paces, look at the face. Non-zero means the pedometer
  finally came on.
- Raise: with the screen asleep, raise the wrist to read the watch. It should
  wake; it should NOT wake from typing or from walking arms-down.

If steps are still 0, the next thing to examine is `ACC_CONF`: Slate writes
`0xA8`, InfiniTime's config computes `0x28` — `perf_mode` differs. That has
**not** been shown to matter, so do not change it without evidence.

## Build, test, install

```bash
"C:\Program Files\CMake\bin\cmake.exe" --build build/dfu
```

```bash
"C:\Program Files\CMake\bin\ctest.exe" --test-dir build/host-tests -E ble_link
```

DFU packaging needs `imgtool` and `adafruit-nrfutil`, which are user-site pip
installs — prepend `C:\Users\highj\AppData\Roaming\Python\Python313\Scripts` to
PATH first:

```bash
"C:\Program Files\CMake\bin\cmake.exe" --build build/dfu --target slate_dfu
```

Output is `build/dfu/slate-dfu.zip`. Keep `build/dfu-prod` linking too.

Companion, from `companion/`:

```bash
./gradlew.bat :sdp-tests:test :app:assembleDebug --offline
```

```bash
C:\Users\highj\AppData\Local\Android\Sdk\platform-tools\adb.exe -s 59171FDCH001LR install -r app\build\outputs\apk\debug\app-debug.apk
```

Two devices are attached, so always pass `-s 59171FDCH001LR` (the Pixel). The
other, `HRBDFUN`, is not the target. Package is `slate.app.debug`.

**Bump `versionCode` in `companion/app/build.gradle.kts` on every installed
build.** A static version makes the on-screen version useless.

Build id = first 12 hex of `sha256(build/dfu/slate_firmware.bin)`, uppercase.

## Traps that have already cost time

- **The on-screen build stamp only changes when `src/local_ui.cpp` recompiles.**
  `touch src/local_ui.cpp` before any build the operator will flash. This bit
  again on 9 August: `88DBB925E697` and `451C3AB52718` both carried stamp
  `11:54`, so the watch could not tell them apart.
- **Always confirm which image is running** before reasoning about a symptom:
  compare the watch's version line against the `__TIME__` string inside
  `build/dfu/slate_firmware.bin`. This has caught a wrong diagnosis already.
- **A test fixture can pass spuriously.** `tests/host/test_bma42x.cpp` first
  filled the feature block with `feature[i] = i`, so byte `0x3B` held `0x3B` —
  which already carries the `0x10` bit under test. The assertion passed against
  a driver that did nothing. Always assert the fixture starts in the state you
  think it does.
- **Gradle prints `BUILD SUCCESSFUL` and then `BUILD FAILED`** on a Windows
  file lock during daemon shutdown. That second line is not a test result. Read
  the truth from `companion/sdp-tests/build/test-results/test/*.xml`.
- **Generated files have been hand-edited.** `generated/SdpWire.kt` held
  constants never present in `shared/sdp_wire.json`; regenerating deleted them
  and broke the build. The generator **cross-checks** `include/sdp_opcodes.hpp`
  rather than writing it, so a new opcode must be added there by hand.
- **Verify every scripted edit landed.** Two `python str.replace` edits in
  heredocs silently matched nothing and were reported as successful; that cost
  the operator a flash. Use the Edit tool, or `assert s.count(old) == 1` before
  replacing, and grep afterwards.
- **Kotlin incremental caches corrupt.** On a bizarre "unresolved reference" to
  something that plainly exists: `gradlew --stop`, delete `*/build/kotlin`,
  rebuild.
- **Bundled demos** only reinstall when the bundled `version` differs.
- **`uiautomator dump` returns "null root node" on this Pixel.** Use
  `adb exec-out screencap -p` instead. Under Git Bash, prefix adb commands that
  carry device paths with `MSYS_NO_PATHCONV=1` or `/sdcard/x` becomes
  `C:/Program Files/...`.
- **adb cannot start non-exported components.** `WatchSettingsActivity` and
  `LinkForegroundService` are `exported="false"`; drive them through the UI.
- **Reinstalling the companion while it is connected orphans the BLE link** —
  the process dies, its GATT clients unregister, the ACL stays up owned by
  nobody. Expect a reconnect after every install. See N-58.

## How to work here

These were learned expensively. They are not style preferences.

1. **Check InfiniTime first when touching a driver.** Do not debug outward from
   the symptom. Six defects have now been divergences; the two on 9 August were
   both found by reading Bosch's and InfiniTime's code, with no hardware round
   trip at all.
2. **Instrument before theorising** — but reading the reference implementation
   is faster than instrumenting when a driver is involved.
3. **The operator's observation outranks your inference.** They can see the
   hardware; you cannot. They have corrected confident wrong explanations more
   than once, and have been right every time.
4. **Verify a function's callers, not just its body.** `Core::on_tap_elem` was
   correct, tested, and had **no caller** — the settings rows were drawn
   perfectly and did nothing (N-57). Grep for callers before packaging.
5. **Distrust success returns from hardware.** An I2C ACK says the sensor
   accepted bytes, not that they landed anywhere useful. This driver has now
   twice certified a pedometer that was switched off. Verify by reading back
   through a path that cannot share the same fault.
6. **A flash costs the operator real time.** Batch firmware changes, state
   exactly what changed, and say what to look for *and what would disprove you*.
7. **Say what is unverified.** Distinguish measured from inferred, every time.
8. **Update `docs/issue-prompts-open.md` as you go**, not in a batch at the end.

## Open items, highest value first

1. **Confirm N-59 / N-60 on hardware** — steps and raise-to-wake, fix built and
   unflashed. Everything else is behind this.
2. **I-19 — RAM margin is 200 bytes**, against `CLAUDE.md`'s stated ≥6 KB slack;
   RAM is at 93.5%. The failure mode is a link-time assert, not corruption, so
   it is loud — but there is no room for the next feature. Reclaiming it means a
   real look at `ucHeap` (16 KB), `g_interp` (8.6 KB), `g_renderer` (7.7 KB) and
   `g_core` (6.4 KB, in `.data` only because of non-zero member initialisers).
   **Do not chase `GlyphCache`** — `--gc-sections` already discards it; a parse
   that "found" it was reading the map's discarded sections.
3. **I-18 — battery percentage jumps down when the charger comes out** (62% → 51%).
   Investigated 8 August and recorded as **not a defect** in the ADC path; the
   findings are written up for whoever fixes the presentation.
4. **N-36 — app-task stall.** Real and recurring. The N-53 band fix moved it a
   long way (stall episodes 4.0/s → 0.54/s, worst stall 714 ms → 310 ms). A
   full-face render is still ~236 ms; the next lever is the same band treatment
   for the remaining full-screen repaints.
5. **`ble_link` host test** — `drop/reject: got 0 want 1`. Pre-existing and
   never investigated. It sits on `sdp_frame.cpp`, the one path nobody has
   examined. **Run ctest with `-E ble_link` and do not read "all tests pass" as
   covering it.**
6. **Credit model** — the watch advertises 4096-or-0, not real free bytes
   (`Manager::free_dl_bytes`, explicitly a placeholder). The host releases
   credit when a screen leaves; real accounting is the proper fix.
7. **Tickless idle (I-3)** remains blocked: only the app-task loop pets the WDT,
   and the bootloader dog is ~7 s.

## What changed on 8–9 August, in one place

Every item is in `docs/issue-prompts-open.md` with full reasoning.

**Firmware**

- Watch settings screen: three tappable rows (raise-to-wake, display timeout,
  show steps), reached by swiping left-to-right from the face.
- `settings_sync.{hpp,cpp}` — 10-byte CONTROL `SETTINGS_SYNC` (0x21), Lamport
  revision, last-writer-wins with the watch taking ties. 22 host tests.
- **N-57**: `Core::on_tap_elem` had no caller; taps on local screens went to the
  phone. Now dispatched via `local_tap_handled()` in `main.cpp`; `on_tap_elem`
  returns `bool` so the screen check stays in one place.
- **N-56**: the sync revision was RAM-only and reset to 0 on every reboot, so
  the phone's older copy won the next merge. Moved into the persisted
  `local::Settings`; magic bumped `'SLTT'` → `'SLTU'`, so **settings return to
  defaults once** on the next flash.
- **N-59**: the step-counter enable bit never reached the sensor — the 70-byte
  feature block was read-modify-written in 8-byte chunks with the ASIC address
  set once, so every chunk hit bytes 0–7 and the verify re-read the same wrong
  bytes and passed. Now single 70-byte transfers.
- **N-60**: `read_accel` returned the chip's axis frame; the BMA is mounted
  rotated and `RaiseDetector`'s thresholds are InfiniTime's, expressed in the
  swapped frame. X and Y are now swapped in the driver, as InfiniTime does.
- `tests/host/test_bma42x.cpp` — new, and the reason both were found. Models a
  sensor that enforces "the ASIC address does not persist across transfers".

**Companion**

- `WatchSettings.kt` — the Kotlin mirror of the sync codec. Its golden vectors
  are the **firmware encoder's actual output**, not a re-derivation.
- `WatchSettingsStore` (process-wide singleton, persisted) and
  `WatchSettingsActivity`.
- `CompositorHost` sends the opening exchange 1200 ms after Ready (N-25: the
  watch's AppInbox holds one message; 1200 ms clears the time-sync retries at
  300/2300/10300 ms) and merges inbound `SETTINGS_SYNC`.
- **N-58**: the reconnect button refused to run whenever the phone held a link
  Slate did not own, and blamed "another app" — a claim `getConnectedDevices`
  cannot support, since an orphaned link looks identical. Three `MainActivity`
  sites now warn and proceed. OTA and sealed DFU still block, deliberately.
- Settings screen layout: `safeDrawingPadding()` (the first row was clipped
  under the system bar — the `targetSdk 35` edge-to-edge trap) and `FlowRow` for
  the timeout chips ("Never" had been compressed into a vertical column of
  letters).

**Incidental edits**, since they are easy to miss: `CMakeLists.txt` and
`tests/host/CMakeLists.txt` (new test targets), `shared/sdp_wire.json` +
`generated/SdpWire.kt` + `include/sdp_opcodes.hpp` (the 0x21 opcode),
`tests/host/test_local_ui.cpp` (a stale assertion that counted text ops and
would have passed on a screen with no tap targets at all).
