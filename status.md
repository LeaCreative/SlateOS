# Session handoff — 2026-07-31 (night)

Context for the next agent picking up Slate/EvoTime development. Read
`CLAUDE.md` first (standing rules), then `issues.md` (live issue register),
then this file for where things stand *right now*.

## Where we are

The sealed PineTime was recovered (I-1), and today's work flashed and
field-debugged five post-recovery Slate images. **The current staged
package is `build/dfu/slate-dfu.zip`, SHA-256 prefix `B7A58CFAC9B6`** —
N-13/N-14/N-15, plus **N-16** (the Slate GATT service was registered after
`ble_gatts_start()`, so it was never in the attribute table) and **N-17**
(the service UUID byte array used the Microsoft GUID mixed-endian layout
instead of NimBLE's full reversal, so the service was published as
`f378f04c-6ee9-62a9-44fa-0000e979acfb` — proven with nRF Connect). Verified on hardware so far: the install is confirmed and durable
(`IMAGE_OK` written), the tick is correct (N-10/N-11), the radio advertises
(N-9), and the battery reads true (N-12: raw 831 → 3895 mV).

N-13 was diagnosed from the overlay (`3.1236` = 1236 ms in the session/core
tick): a full face render re-parses the display list once per tile, and the
app task outranked the NimBLE host, so that render blocked ATT — which is
why MTU stayed 23, discovery never finished, the link dropped, and the
benchmarks could not send. Fix staged; **verifying it is the next step.**

N-9 (silent radio) is **resolved and verified on hardware**: advertising is
up, nRF Connect sees "SLATE" at `E8:01:34:22:08:89` and connects.

### Expected outcome when the user flashes `C3AE3F15E408`

- No more watchdog reboots (~30 s cycle on the previous image). Reset
  reason drops back to `4` (soft) — it is a raw RESETREAS bitmask and
  **accumulates**, so bit 2 (=`6`) means the dog bit.
- Paints should track uptime ÷ 0.32 s (previous image: 3 paints in 22 s —
  the tick was ~20x slow). 7th field (recovered ticks) climbing is the
  proof N-10 was real; it quantifies time that used to vanish.
- Then: keep any central connected ~10 s → amber trial bar clears →
  `IMAGE_OK` confirmed. This would be the first durable Slate install.
- Diag overlay full format (see `fmt_diag` in `src/local_ui.cpp`):
  `reset/uptime_s/paints/button/worst_stall_ms/ble_state.rc/recovered_ticks`
  BLE state 7 = advertising; 1–6 = stage reached; 90–95 = failure with rc
  (map in `ble::bringup_snapshot`, `include/ble_gatt.hpp`).

## What was done this session (all verified, host tests green)

| Item | Status | Summary |
|---|---|---|
| N-4 | Resolved | Single-in-flight frame reassembly: a FIRST on any channel aborts an in-flight message on another channel (shared buffer, reject-and-resync, `preempt_drop_count()`). Companion: new `SdpWriteQueue` (sdp-core) enqueues each message's fragments atomically; `SdpReassembler.kt` mirrors the rule. Spec added to roadmap §4.2. |
| N-8 | Resolved | ARM link had been broken since N-1 (+4.1 KiB AppInbox pushed heap past MSP stack reserve, 3,600 B over). Fix: `AppInbox` is now **zero-copy** — borrows the reassembler buffer, `Link::on_rx_write` gates ALL ingest (incl. DIAG) while a message is pending. RAM back to 60,944 B (92.99 %), 496 B below `__StackLimit`. |
| N-6 | Resolved | `encode_hello_offer` hardened: capacity-checked `push`/`push_u16`/`push_bytes`, overflow → return 0; `static_assert` (`kHelloOfferMaxBytes` ≤ `kHelloOfferBufBytes`, `session.hpp`) so catalog growth fails the build. Byte-exact 84-B golden pinned on BOTH sides: `test_session.cpp` and Kotlin `HelloOfferGoldenTest` (fixture + `parseHelloOffer`). |
| N-11 | Fix staged | **The tick IRQ ran at priority 0.** `NVIC_SetPriority(RTC1, configKERNEL_INTERRUPT_PRIORITY)` double-shifted: CMSIS shifts internally and that macro is already the shifted register form, so `(7<<5)<<5 & 0xFF` = 0. The tick sat above the radio (5) and above `configMAX_SYSCALL` (3), so `portENTER_CRITICAL`'s BASEPRI couldn't mask it and `xTaskIncrementTick` could corrupt kernel lists inside a task's critical section. Latent since the port was written; found because N-10's catch-up added the first self-validating FromISR call (cyan `port.c:890`). Fixed by passing the raw priority + `_Static_assert`. |
| N-10 | Fix staged | With the radio live, the watch watchdog-rebooted every ~30 s. The RTC1 tick ISR runs at the lowest IRQ priority, below NimBLE's RADIO (5); TICK events coalesce while it is held off, and this port had **no catch-up**, so the FreeRTOS clock free-ran ~20x slow. Tick-scheduled work (incl. `app_loop`'s 20 ms wait → the WDT pet cadence) stretched past the bootloader's real-time 7 s dog. Fix: restore InfiniTime's `(COUNTER − tickCount)` catch-up with hard guards (tick-ahead → step 1, never ~16M; ≤128 ticks/ISR). Recovered ticks shown as diag field 7. |
| N-9 | **Resolved, verified on hardware** | Flashed Slate had a silent radio. Telemetry image read `93.21` = `ble_gap_adv_start` → `BLE_HS_ENOADDR`: no identity address configured (nRF52 has no public addr; standalone port never set the FICR static-random identity — same gap class as the missing `ble_ll_init` sysinit). Fix in `on_sync`: static-random addr from `FICR.DEVICEADDR` (stable across boots for CDM/bonds), `ble_hs_id_set_rnd` + `ble_hs_id_infer_auto`; both adv starts use the inferred type. BLE bring-up telemetry is permanent (diag field 6). |

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

**`docs/issue-prompts-open.docx` is the current register** — 13 agent-ready
prompts covering the 14 open issues, consolidating the still-open audit
items with the hardware findings, plus an overlap map of work that must not
be done twice. It supersedes `docs/issue-prompts.docx` (morning audit;
DONE items removed) and rewrites I-3, I-9, I-10, I-13 and I-14, whose
premises tonight's findings invalidated — do not work from the old copies.
`issues.md` remains the live status register.

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
