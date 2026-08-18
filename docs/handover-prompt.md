# Handover prompt — paste this to the next instance

You are picking up work on **Slate** (this repository), a thin-client smartwatch
OS for the PineTime: nRF52832 firmware in C++17 plus an Android companion in
Kotlin. The watch is **sealed — no SWD**. The only way to see what it is doing
is the on-screen diagnostic overlay and the companion's in-app log, both of
which the operator photographs and pastes to you.

Written **18 August 2026**. Replaces the 10 August version.

## Read these first, in this order

1. `CLAUDE.md` — standing rules. The hardware pin map is authoritative; do not guess it.
2. `docs/capabilities.md` — **what the software can do today** (firmware + companion).
   Read this before assuming a feature is missing or still broken.
3. `docs/issue-prompts-open.md` — **single point of truth** for open work and
   current state. `issues.md` and `status.md` are historical; where they
   disagree, that file wins. Its newest section is at the top.
4. `docs/subapp-rules.md` — normative for JS sub-apps. Read before touching one.
5. `docs/infinitime-parity.md` — what mirrors InfiniTime, what deliberately does not.

Reference tree for low-level parity: a local checkout of
[InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime) (same revision you
last used for driver diffs).
**Read it before debugging any driver.** Every driver defect this project has
had was a divergence from it.

## State as of 18 August 2026

| | |
|---|---|
| **Firmware packaged** | `build/dfu/slate-dfu.zip` SHA-256 prefix **`EDAC341E7A03`** (`0.1.0-m21 Aug 18 15:56`, MCUBoot **0.1.21**) |
| **Wrist** | **m21 booted**. Equal-version `IMAGE_OK` was **not** the stall (m16–m18 already did that at `ih_ver` 0.1.0; m20 was 0.1.20 and still did not boot) |
| **Companion on Pixel** | **`0.8.2-p85`** / versionCode **86** |
| **Host tests** | Run with `-E ble_link`; `ble_link` still fails (`drop/reject`) |
| **RAM link slack** | **~3016 B** (~89% static) after I-19 ScreenBlock 3072→256 |

**Working tree:** uncommitted work is common. Do not `stash`, `reset`, or
`checkout` source to “get a baseline” unless the operator asks — you will
destroy work. Do not commit unprompted.

### Confirmed working on hardware

Taps and the app launcher; JS sub-apps (timer, nav, camera, vibrate, location,
map); OSM vector map; SDP OTA; display sleep (20 s default); wake on charger
edges; watch settings + companion settings sync; **steps (non-zero)**;
**raise-to-wake** (after m18 DFU — m17 had ACC zeros); no false-wake on typing /
arms-down walk; settings↔face and launcher↔face swipe routing.

### Companion Open-with

`SideloadActivity` (label **Open with Slate**) classifies zips via `ZipIntake`:
sub-app → install; `slate-dfu.zip` → `SlateOtaActivity` with URI pre-selected.
Sealed InfiniTime→Slate install stays on the main-screen button (not Open-with).

## Build, test, install

```bash
"C:\Program Files\CMake\bin\cmake.exe" --build build/dfu --target slate_firmware.elf
```

```bash
"C:\Program Files\CMake\bin\ctest.exe" --test-dir build/host-tests -E ble_link
```

DFU packaging needs `imgtool` and `adafruit-nrfutil` on PATH (user-site Scripts):

```bash
"C:\Program Files\CMake\bin\cmake.exe" --build build/dfu --target slate_dfu
```

Output: `build/dfu/slate-dfu.zip`. DFU zip id for the operator = first 12 hex of
`sha256(slate-dfu.zip)` uppercase (or of `slate_firmware.bin` — be consistent
with what you tell them; recent notes use the **zip**).

Companion, from `companion/`:

```bash
./gradlew.bat :sdp-tests:test :app:installDebug
```

Always pin the operator's Pixel with adb (`adb devices`, then `-s <serial>`).
Do not hard-code a device serial in docs or scripts.

```bash
adb -s <pixel-serial> …
```

Two devices may be attached; confirm which is the development phone. Package
`slate.app.debug`.

**Bump `versionCode` in `companion/app/build.gradle.kts` on every installed
build.**

Touch `src/local_ui.cpp` before any firmware flash build so the on-screen time
stamp changes.

## Traps that have already cost time

- **On-screen build stamp only changes when `src/local_ui.cpp` recompiles.**
- **Confirm which image is running** before diagnosing (version line vs bin/`__TIME__`). The version line is **Face diag On only** from m19.
- **Face diag Off still showed the version** until m19 — do not treat a missing version as “old firmware” after that flash.
- **OsmAnd continue is not arrival.** Cyan bar / caret was “go ahead”; destination reached is a checkmark + copy (`slate.navigation` 1.2.2).
- **Test fixtures can pass spuriously** — see `test_bma42x` history (bit already set).
- **Gradle** may print SUCCESS then FAILED on a Windows daemon lock; trust JUnit XML.
- **`generated/SdpWire.kt`** — generator cross-checks opcodes; add new opcodes in
  `include/sdp_opcodes.hpp` by hand.
- **Verify scripted edits landed** (Edit tool / assert before replace).
- **Kotlin incremental caches** — `--stop`, delete `*/build/kotlin` if absurd errors.
- **Bundled demos** reinstall only when bundled `version` differs.
- **`uiautomator dump`** often null on this Pixel — use `screencap`.
- **adb cannot start non-exported components** — drive Watch settings / FGS via UI.
- **Reinstalling the companion while linked orphans BLE** — expect reconnect (N-58).
- **ACC_CONF is `0x28` now** (matched InfiniTime). Older notes saying `0xA8` are stale.
- **Raise-to-wake is software `RaiseDetector`**, not a BMA hardware tilt IRQ.

## How to work here

1. Check InfiniTime (and Bosch) first when touching a driver.
2. Instrument before theorising — but reference code beats guessing for drivers.
3. Operator observation outranks inference.
4. Verify callers, not just function bodies.
5. Distrust hardware “success” without a distinct verify path.
6. Batch flashes; say what to look for and what would disprove you.
7. Update `docs/issue-prompts-open.md` (and `docs/capabilities.md` when
   capability changes) as you go.

## Open items, highest value first

1. **Confirm `9421B271FDC3` on hardware** if not yet flashed — step accuracy after
   ACC_CONF `0x28`, swipe settings↔face↔launcher.
2. **N-36** — remaining full-face repaint stalls.
3. **`ble_link` host test** — `drop/reject`; run with `-E ble_link`.
4. **Credit model** — real `free_dl_bytes`, not 4096-or-0.
5. **I-18** — battery % presentation when charger unplugs (ADC path OK).
6. **I-3** — tickless idle blocked (WDT only from app task).
7. **N-34** — Ambient ClockApp policy if ambient focus returns.
8. Further I-19 reclaim toward ≥6 KB aspirational slack (3016 B is usable; not done).

## What landed 8–10 August (index)

Full write-ups remain in `docs/issue-prompts-open.md`. Headline:

- Watch settings + `SETTINGS_SYNC`; N-57 tap dispatch; N-56 persisted revision.
- N-59 / N-60 pedometer + axis swap — **HW confirmed**; ACC_CONF → `0x28`.
- I-19 ScreenBlock 3072→256 (~2.8 KiB reclaim).
- Swipe routing fix (settings/launcher no longer steal each other).
- Companion Watch settings UI; dual ZIP Open-with (sub-app vs SDP OTA).
- Bundled JS: timer, nav, camera, vibrate, location, map.
