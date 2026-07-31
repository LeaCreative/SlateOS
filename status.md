# Session handoff — 2026-07-31 (night)

Context for the next agent picking up Slate/EvoTime development. Read
`CLAUDE.md` first (standing rules), then `issues.md` (live issue register),
then this file for where things stand *right now*.

## Where we are

The sealed PineTime was recovered (I-1), and today's work flashed and
field-debugged the first post-recovery Slate images. **The current staged
package is `build/dfu/slate-dfu.zip`, SHA-256 prefix `4324FC495E0C`** —
it contains the N-9 fix (see below) and is **awaiting on-watch
verification**. That verification is the immediate next step.

### Expected outcome when the user flashes `4324FC495E0C`

- Diag overlay (top line of the watch face) 6th field should read `7.0`
  → advertising up, "Slate" visible in nRF Connect.
- Then: keep any central connected ~10 s → amber trial bar clears →
  `IMAGE_OK` confirmed. This would be the first durable Slate install.
- If instead the field reads something else, decode via
  `ble::bringup_snapshot` in `include/ble_gatt.hpp`: 1–6 = bring-up stage
  reached, 90–95 = named failure with rc appended (`state.rc`).
- Diag overlay full format:
  `reset_reason/uptime_s/paints/button/worst_stall_ms/ble_state.rc`
  (see `fmt_diag` in `src/local_ui.cpp`).

## What was done this session (all verified, host tests green)

| Item | Status | Summary |
|---|---|---|
| N-4 | Resolved | Single-in-flight frame reassembly: a FIRST on any channel aborts an in-flight message on another channel (shared buffer, reject-and-resync, `preempt_drop_count()`). Companion: new `SdpWriteQueue` (sdp-core) enqueues each message's fragments atomically; `SdpReassembler.kt` mirrors the rule. Spec added to roadmap §4.2. |
| N-8 | Resolved | ARM link had been broken since N-1 (+4.1 KiB AppInbox pushed heap past MSP stack reserve, 3,600 B over). Fix: `AppInbox` is now **zero-copy** — borrows the reassembler buffer, `Link::on_rx_write` gates ALL ingest (incl. DIAG) while a message is pending. RAM back to 60,944 B (92.99 %), 496 B below `__StackLimit`. |
| N-6 | Resolved | `encode_hello_offer` hardened: capacity-checked `push`/`push_u16`/`push_bytes`, overflow → return 0; `static_assert` (`kHelloOfferMaxBytes` ≤ `kHelloOfferBufBytes`, `session.hpp`) so catalog growth fails the build. Byte-exact 84-B golden pinned on BOTH sides: `test_session.cpp` and Kotlin `HelloOfferGoldenTest` (fixture + `parseHelloOffer`). |
| N-9 | Fix staged | Flashed Slate had a silent radio. Telemetry image read `93.21` = `ble_gap_adv_start` → `BLE_HS_ENOADDR`: no identity address configured (nRF52 has no public addr; standalone port never set the FICR static-random identity — same gap class as the missing `ble_ll_init` sysinit). Fix in `on_sync`: static-random addr from `FICR.DEVICEADDR` (stable across boots for CDM/bonds), `ble_hs_id_set_rnd` + `ble_hs_id_infer_auto`; both adv starts use the inferred type. BLE bring-up telemetry is permanent (diag field 6). |

Hardware findings today (worth knowing):
- Button-hold revert **works on hardware** — first field proof of the
  R-3/N-2 WDT-starve design. Unconfirmed images are safely revertible.
- Worst app-loop stall observed ~1.6 s during BLE bring-up (NimBLE ll/host
  outrank the app task). Not yet investigated; benign so far.
- This is the first hardware run of NimBLE on the RTC1 tick port — BLE had
  never been exercised on it before today.

## Package history (also in `docs/flash-sealed.md`)

| SHA prefix | Meaning |
|---|---|
| `CC3F25A12AB2`, `790822E97A78` | **Bricking images — never flash** |
| `8B2D9054E9E7` | Pre-N-1 tree, superseded, never flashed |
| `D7E86854B9D3` | Flashed: boots/paints, radio silent, no diag |
| `2284BA304B8F` | Flashed: telemetry build that produced `93.21` |
| `4324FC495E0C` | **Current staged fix — flash this** |

Companion APK: `companion/app/build/outputs/apk/debug/app-debug.apk`
(built tonight, includes `SdpWriteQueue`; wire-compatible with all firmware).

## Remaining open issues (see `issues.md` for detail)

N-9 verification → then I-7/I-8 (companion DFU UX), I-15, I-13 (OTA proof
on hardware), I-3 (tickless soak), I-9/I-14 (ongoing audit/RAM watch),
I-10 rest, I-11, I-12. `docs/issue-prompts.md` has agent-ready prompts.

## Environment gotchas (Windows)

- Host tests: `cmake --build build/host-tests` then
  `ctest --test-dir build/host-tests`. cmake is NOT on the agent shell PATH:
  use `C:\Program Files\CMake\bin\cmake.exe`.
- ARM builds: `build/dfu` and `build/dfu-prod` (both Release, keep both
  linking). DFU packaging (`--target slate_dfu`) needs
  `C:\Users\highj\AppData\Roaming\Python\Python313\Scripts` on PATH
  (imgtool + adafruit-nrfutil live there).
- RAM is knife-edge: heap end is 496 B below the MSP stack reserve. Any
  static growth needs a map check (`build/dfu/slate_firmware.map`,
  `__heap_end__` vs `__StackLimit`). The linker asserts on collision.
- Companion: `cd companion; .\gradlew.bat :sdp-tests:test` (JVM tests) and
  `:app:assembleDebug`. User's shell is PowerShell 5.1 — no `&&`.
- The repo is a git repo but nearly all recent work is **uncommitted**
  (HEAD is old). Do not trust HEAD as a baseline; do not stash source files
  casually — a stash-revert briefly resurrects the pre-N-1 tree.
- Sealed watch = no SWD. The diag overlay and RTT-less telemetry patterns
  (like `bringup_snapshot`) are the only debug windows. Keep return codes
  captured, never discarded.
