# Handover prompt — paste this to the next instance

You are picking up work on **Slate** (repo: `C:\Users\highj\Documents\Projects\EvoTime`),
a thin-client smartwatch OS for the PineTime: nRF52832 firmware in C++17 plus an
Android companion in Kotlin. The watch is **sealed — no SWD**. The only way to
see what it is doing is the on-screen diagnostic overlay and the companion's
in-app log, both of which the operator photographs and pastes to you.

## Read these first, in this order

1. `CLAUDE.md` — standing rules. Hardware pin map is authoritative; do not guess it.
2. `docs/subapp-rules.md` — **normative** for JS sub-apps. Your first task is built on it.
3. `docs/issue-prompts-open.md` — single point of truth for open work and current
   state. `issues.md` is historical and partial; where they disagree this wins.
4. `status.md` — handover snapshot.
5. `docs/infinitime-parity.md` — what mirrors InfiniTime, what deliberately does not.

Reference tree for low-level parity: `C:\Users\highj\Documents\Projects\InfiniTime-main`.

## State as of 6 Aug 2026, 23:30

| | |
|---|---|
| **Firmware on the watch** | `609EF4F6979A` (stamp `10:43`) |
| **Built, NOT flashed** | `AB044776E1FE` (stamp `23:04`) — several builds' work queued behind one flash |
| **Companion installed** | `0.6.1-p24 (build 25)` on the Pixel |
| **Host tests** | 18/18 **only with `-E ble_link`** — that test genuinely fails |

Working end to end: taps, the app launcher (swipe left opens, tap launches,
swipe right closes), JS sub-apps, phone vibration from the watch, OTA.

## Your first task

**Bring every existing sub-app into conformance with `docs/subapp-rules.md`.**

Sub-apps live in `companion/examples/`:

| App | State |
|---|---|
| `timer` | **Reference implementation.** Conforming header, declares `durationSec`. Copy its shape |
| `vibrate` | Header is close but predates the 5-point format; no settings declared |
| `camera`, `navigation` | UI shells with no function. Headers do not conform |
| `image` | Operator's. **3987 B display list** — over the 2 KB practical limit in §2, and the reason N-46 was found. Needs a budget note at minimum, ideally a smaller screen |
| `image-vector` | Operator's. **This app reset the watch** (N-44/N-45). Firmware is fixed, but the app should carry a budget note explaining why it is expensive |

For each: conforming header (§4), bounded loops (§2.2), declared permissions
that match actual use, BACK handled, and a measured display-list size. Add
settings where a value is obviously user-tunable — but do not invent settings
for the sake of it.

`image` and `image-vector` are the operator's own work. Do not rewrite their art;
report what exceeds a limit and propose, rather than silently changing it.

## Build, test, install

```bash
# cmake is not on PATH
"C:\Program Files\CMake\bin\cmake.exe" --build build/dfu
"C:\Program Files\CMake\bin\ctest.exe" --test-dir build/host-tests -E ble_link
# DFU packaging needs imgtool/adafruit-nrfutil on PATH:
#   C:\Users\highj\AppData\Roaming\Python\Python313\Scripts
"C:\Program Files\CMake\bin\cmake.exe" --build build/dfu --target slate_dfu
```

Companion (from `companion/`): `.\gradlew.bat :app:assembleDebug --offline -q`,
then install to the **Pixel by serial** — two devices are attached:

```
C:\Users\highj\AppData\Local\Android\Sdk\platform-tools\adb.exe -s 59171FDCH001LR install -r app\build\outputs\apk\debug\app-debug.apk
```

Bump `versionCode` in `companion/app/build.gradle.kts` on **every** installed
build. Package is `slate.app.debug`.

## Traps that have already cost time

- **The on-screen build stamp only changes when `src/local_ui.cpp` recompiles.**
  Touch that file before a build the operator will flash, or two different
  images show the same time and you cannot tell what is running.
- **Always confirm which image is running** before reasoning about a symptom:
  compare the watch's version line against the `__TIME__` string inside
  `build/dfu/slate_firmware.bin`. This has caught a wrong diagnosis already.
- **Generated files have been hand-edited.** `generated/SdpWire.kt` held
  constants that were never in `shared/sdp_wire.json`; regenerating deleted them
  and broke the build. Assume others carry the same debt.
- **Bash heredocs mangle `\n` in Python.** Write scripts to the scratchpad with
  the Write tool and run them; do not inline multi-line Python in a heredoc.
- **Kotlin incremental caches corrupt.** On a bizarre "unresolved reference" to
  something that plainly exists: `gradlew --stop`, delete `*/build/kotlin`,
  rebuild. Verify the APK actually contains your change before handing it over.
- **Bundled demos** only reinstall when the bundled `version` differs. Bump it
  or a manifest edit never reaches the device.

## How to work here

These were learned expensively today. They are not style preferences.

1. **Instrument before theorising.** Three separate wrong theories were shipped
   against the dead-touch bug because the counter said *that* a read failed and
   never *how*. Every silent failure path cost a round trip. If something fails
   invisibly, make it name itself first, then fix it.
2. **The operator's observation outranks your inference.** They can see the
   hardware; you cannot. Twice today they corrected a confident wrong
   explanation. When they say something worked before, believe it and re-derive.
3. **Check every reading you already have before filing a defect.** N-42 was
   filed off one anomalous value while six contradicting readings sat in the
   same conversation. It was withdrawn.
4. **A flash costs the operator real time.** Batch firmware changes, state what
   changed, and say precisely what to look for on the overlay and what would
   disprove you.
5. **Say what is unverified.** Distinguish measured from inferred, every time.

## Open items, highest value first

- **N-36** — app-task stall. Real and recurring (~3 episodes ≥100 ms per second
  after excluding sleep). `core.tick` double-repaint and the 2 s full-face diag
  repaint are fixed; a full-face render is still ~236 ms. Next lever: the same
  band treatment for the remaining full-screen repaints.
- **N-41 / N-35 / N-43** — OTA resyncs, watch stops replying, supervision
  timeouts. Expect N-36 to move all three; re-measure before treating separately.
- **`ble_link` host test** — `drop/reject: got 0 want 1`. Never investigated,
  sits on `sdp_frame.cpp`, the one path nobody has examined.
- **Credit model** — the watch advertises 4096-or-0, not real free bytes
  (`Manager::free_dl_bytes`, explicitly a placeholder). The host now releases
  credit when a screen leaves, but real accounting is the proper fix.
