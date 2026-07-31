# EvoTime / Slate — implementation prompts per open issue

Generated 2026-07-31 from `issues.md`, the EvoTime tree, and in-depth reading of
InfiniTime (`SystemTask.cpp`, `port_cmsis_systick.c`, `BatteryController.cpp`,
`NimbleController.cpp`) and the pinetime-mcuboot-bootloader. Each prompt is
self-contained — paste it into Claude Code (or any agent) opened at the repo root.

Policy (sharpened 2026-07-31): at the low level — flash, update/DFU, BLE bring-up,
boot, WDT/button, power — **mirror InfiniTime verbatim**; it works, so the default
verdict is *mirror*, not *decide*. Divergence at that level is an exception that
must carry a documented sealed-watch rationale (currently one: the IMAGE_OK
connection-dwell, I-6). Slate differs on purpose only above that line, where the
companion's JS apps tell the watch what to draw (SDP display lists, local tiles,
input events).

---

## I-1 — Sealed watch soft-brick (recovery runbook + regression guard)

> The sealed PineTime running Slate DFU image SHA `790822E97A78` does not advertise and could not be reset by button hold, because that image petted the bootloader WDT from the FreeRTOS tick path unconditionally. Recovery is user-driven (discharge → bootloader button hold), but the repo needs the procedure written down and the regression fenced off. Do three things:
>
> 1. Write `docs/recovery-sealed.md`: a step-by-step recovery runbook for a Slate soft-brick with no BLE. Base it on the pinetime-mcuboot-bootloader boot flow: on reset the bootloader shows the pine cone for ~5 s; keep holding the side button until the logo fills **blue** to revert to the previous (InfiniTime) image, or keep holding until **red** to load the recovery firmware (a stripped InfiniTime with OTA). Cover: forcing a reset via full discharge when WDT starvation is unavailable, catching the pinecone on the charger-induced boot, DFU-ing a known-good InfiniTime zip from recovery, and only then flashing a fixed Slate package. Cross-link `docs/flash-sealed.md`.
> 2. Verify the in-tree WDT fix chain end-to-end and document it in the runbook's "why this can't recur" section: `slate::wdt::pet()` in `src/wdt.cpp` returns without reloading RR0 while `board::button_raw()` is true, and every pet path funnels through it — `vApplicationTickHook` and `vApplicationIdleHook` in `src/main.cpp`, `pet_service()`, and `configPOST_SLEEP_PROCESSING` → `slate_wdt_pet` in `config/FreeRTOSConfig.h`. This mirrors InfiniTime `SystemTask::Work()`, which reloads the watchdog only when `nrf_gpio_pin_read(PinMap::Button) == 0`.
> 3. Add a host-side unit test (under `tests/host`) that fails if any code path reloads the WDT while the button reads pressed — e.g. compile `wdt.cpp` against a fake `board::button_raw()` and assert RR0 is untouched during a simulated hold.
>
> Do not change flash-map, MCUBoot, or DFU behaviour in this task. Record both bad package SHAs (`790822E97A78`, `CC3F25A12AB2`) in the runbook as known-bricking images.

---

## I-2 — Package and stage the fixed Slate DFU

> Prepare the post-recovery Slate DFU package. The fixes are already in tree; your job is to prove they are all present, build, and record the artifact. Steps:
>
> 1. Assert the fix set with greps and a short report: (a) `src/wdt.cpp` withholds RR0 reload while the button is held; (b) button enable is left high InfiniTime-style (`src/button.cpp` comment "Enable stays high", `board::button_hw_init`); (c) `port/nrf52/src/port_rtc_tick.c` increments exactly one FreeRTOS tick per RTC1 TICK event with **no** `(COUNTER - xTaskGetTickCount())` catch-up in the ISR; (d) `config/FreeRTOSConfig.h` has `configUSE_TICKLESS_IDLE 0`.
> 2. Build per `docs/flash-sealed.md`: `cmake -S . -B build/dfu -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DBOOTLOADER_PRESENT=ON`, then targets `slate_firmware` and `slate_dfu` (imgtool `--slot-size 475136`, the InfiniTime contract — not Slate ECDSA signing).
> 3. Record the new `slate-dfu.zip` SHA-256 in `issues.md` under I-2 and in `docs/flash-sealed.md`, alongside the two known-bad SHAs, so the field operator can positively identify the fixed package.
> 4. Flash order stays: recovery/InfiniTime first (I-1), then this zip; confirm via amber bar → 10 s central connection → `IMAGE_OK` (`kConfirmDwellMs` in `include/boot_util.hpp`).
>
> Constraint: no functional code changes in this task — if any of the four asserts in step 1 fails, stop and report instead of fixing silently.

---

## I-3 — Tickless idle on RTC1: verification + soak plan before enabling

> `configUSE_TICKLESS_IDLE` is 0 because the first enable regressed (tick ISR catch-up underflow → ISR spin → bootloader WDT revert at ~7 s). The suppress path in `port/nrf52/src/port_rtc_tick.c` was since rewritten. Before it can be enabled on sealed hardware, review and test it against the two reference implementations:
>
> 1. **Diff against the Nordic/InfiniTime port** (`InfiniTime src/FreeRTOS/port_cmsis_systick.c`, `FREERTOS_USE_RTC` branch). Under the mirror rule the stock port is the **default**: keep Slate's variant only if a host test demonstrates the stock catch-up failing in Slate's configuration (RTC1 shared with `wall_clock`, 1024 Hz tick); otherwise revert to the stock port verbatim. The differences to validate: InfiniTime's tick ISR auto-corrects with `diff = (COUNTER - xTaskGetTickCount()) & portNRF_RTC_MAXTICKS` and its suppress path calls `vTaskStepTick(diff)` for the full delta; Slate instead does one increment per TICK event and, in suppress, `vTaskStepTick(diff - 1)` + one `xTaskIncrementTick()` (Nordic Q&A 63828 pattern) precisely to avoid the 24-bit underflow spin. Confirm the Slate combination can never leave `xTickCount` ahead of a pending TICK event (double-count) or behind COUNTER (drift), including: wake exactly on CC0, early wake by another IRQ, `diff == 0`, `diff == 1`, 24-bit COUNTER wrap, and `eTaskConfirmSleepModeStatus() == eAbortSleep`.
> 2. Build a host-side simulation test (in `tests/host`) of `vPortSuppressTicksAndSleep` with a mocked RTC register block, covering the cases above plus the `configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING` 1 s cap and `configPOST_SLEEP_PROCESSING` → `slate_wdt_pet` (WDT always fed within the bootloader's ~7 s window, but still withheld during button hold — verify the hold-to-reset path works *through* a tickless sleep).
> 3. Check the CC0 programming margin: RTC COMPARE events can be missed if CC is set within ~2 counts of COUNTER (nRF52 RTC behaviour); `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` is 2 — show this is sufficient or add an explicit guard.
> 4. Write the soak checklist into `docs/freertos-tickless.md`: sealed watch, ≥24 h: (a) button hold resets via bootloader WDT while idle-sleeping, (b) BLE connect/disconnect/reconnect stays healthy, (c) wall-clock drift vs phone < 2 s/day, (d) no cyan asserts, (e) paints continue. Only after that checklist passes may `configUSE_TICKLESS_IDLE` flip to 1 — do not flip it in this task.

---

## I-4 — Convert remaining local UI screens to readable scaled text

> `src/local_ui.cpp`: `build_face` already uses `text_big` (op `TEXT_SCALED` 0xE0, length-prefixed extension range) but `build_notifs`, `build_settings`, `build_alert` and `build_disconnected` still emit 3×5 font-0 `TEXT` via `text_digits`, which is unreadable on the 240×240 panel. Convert them:
>
> 1. Replace every `text_digits` call in those four builders with `text_big` at scale ≥3 for body text (the clock face uses scale 8 for HH:MM, 3 for date/steps — follow that hierarchy). Re-layout each screen's fixed coordinates so scaled runs fit 240 px; note the in-file warning that a centred run wider than the panel yields a negative x and is rejected by the validator, so left-align long lines like the face's diag overlay does (scale 2, LEFT).
> 2. Respect the font-0 limitation (codepoints 45..58 — digits plus `-./:` only, no A–F); keep all strings numeric.
> 3. Keep `sdp::op` usage within the frozen base opcodes and the 0xE0–0xEF extension range; `TEXT_SCALED` payloads carry a u16 length after the opcode — reuse the existing `text_big` writer, don't hand-roll.
> 4. Check the display-list size budget: builders write into a caller-provided buffer and return 0 on overflow (`W::ok`). Scaled notif rows at scale 3 with 6 visible rows must still fit — measure and, if needed, reduce visible rows or scale.
> 5. Update or add host-side golden tests for the four builders (the repo convention: everything BLE/SDP-facing gets a desktop test), and bump any goldens shared with the companion Kotlin DSL only if these local screens are covered there.
>
> Acceptance: no `text_digits` call sites remain except intentionally tiny markers; all four screens legible at arm's length; host tests pass.

---

## I-5 — Battery percent shows 0 while charging

> Symptoms: on charge the face shows the charge bolt but `0` percent; percent is 0 until the first valid SAADC sample. Current code: `src/battery_hw.cpp` samples once in `init()`; `src/main.cpp` resamples only every 60 s; `src/battery.cpp` maps 3.30–4.20 V linearly and returns 0 when `sample_valid()` is false — indistinguishable from a genuinely flat battery. Mirror InfiniTime's `BatteryController` behaviour:
>
> 1. **Resample on charge events.** InfiniTime calls `batteryController.ReadPowerState()` + a voltage measurement on `OnChargingEvent` (charging-pin GPIOTE toggle) as well as on a periodic timer. Add an immediate `power::sample_battery_adc()` → `battery_hw::set_adc_raw()` when the charging state toggles (hook it where `local_core.cpp Core::poll_battery()` detects `chg != was`, or via a pin-sense event if available). Also shorten the steady-state cadence from 60 s toward InfiniTime's periodic measurement, provided the SAADC busy-wait (≤10 ms in `power.cpp`) stays acceptable in the app loop.
> 2. **Distinguish "unknown" from "0 %".** Give `battery::percent()` an explicit invalid signal (e.g. return `0xFF` or add `percent_valid()`), and render a placeholder instead of `0` until the first valid sample (font 0 has `-` at codepoint 45, so `--` is available). Make sure the OTA/DFU 30 % battery gates (`ota_xfer`, companion preflight) treat "unknown" as *blocked*, not as 0-and-blocked-with-wrong-message.
> 3. **Adopt InfiniTime's charging clamp and hysteresis** — battery measurement is low-level, so under the mirror rule this is required, not optional: they clamp charging display to 99 % until full, and only let percent move up while plugged / down while unplugged, using a 6-point curve `{3500:0, 3616:3, 3723:22, 3776:48, 3979:79, 4180:100}` (mV→%) rather than a straight line. Port the curve into `battery.cpp` behind the existing hook interface so the host tests can cover it. Note InfiniTime's ADC math differs (gain 1/4, ×8×600/1024) — Slate's `adc * 2000 / 1241` conversion must be validated against a multimeter before reusing their curve points verbatim.
>
> Acceptance: plugging in updates percent within ~1 s; watch never displays a numeric 0 while a valid sample says otherwise; host test covers curve, clamp, hysteresis, and the unknown state.

---

## I-6 — IMAGE_OK confirm vs the single BLE connection slot

> Confirming a trial image requires *any* central to hold the only connection for `kConfirmDwellMs` (10 s, `include/boot_util.hpp`); Gadgetbridge/Amazfish auto-reconnect can occupy the slot, and operators can miss the amber bar entirely. InfiniTime handles this with an explicit user action (Settings → Firmware → Validate, `FirmwareValidator` writes the IMAGE_OK word). Confirm mechanics sit at the boot/DFU layer, where the standing rule is to mirror InfiniTime — so the dwell design must be treated as the project's **one documented exception**, kept because it proves the radio can be reached before the point of no return, whereas InfiniTime's manual Validate assumes a local settings UI the thin client deliberately lacks (see `docs/flash-sealed.md`). Record it as such in the I-9 parity table, and improve the *visibility and conflict handling* around it rather than the mechanism:
>
> 1. **Firmware:** expose trial/confirm state to the companion. Add a read-only status the companion can query on connect (extend the HELLO/diag path in `session.cpp` or a CONTROL op — wire constants only in `include/sdp_opcodes.hpp`, new values in the 0xE0–0xEF-skippable extension space): `{needs_confirm, dwell_ms_remaining}` from `boot::needs_confirm()` and `tick_confirm`'s timer. Do not change confirm semantics.
> 2. **Companion:** after any successful connection, surface a prominent "image on trial — keep connected, N s to confirm" countdown, then a "confirmed — image is permanent" state change. If HELLO succeeds but the state never clears, warn.
> 3. **Companion conflict detection:** before sealed-DFU or first-connect flows, query `BluetoothManager.getConnectedDevices(GATT)` for the watch address; if another app holds it (or connects fail while the watch is advertising), show the explicit remediation from `docs/flash-sealed.md`: disable Gadgetbridge/Amazfish auto-reconnect or unpair, then retry. Reuse this in the I-16 work.
> 4. Update `docs/flash-sealed.md` step 8 to mention the in-app countdown.
>
> Acceptance: an operator can tell from the phone, without looking at the watch, whether the image is trial or confirmed, and gets an actionable message when another app owns the slot.

---

## I-7 — Nordic DFU reports failure on a successful swap

> `companion/app/src/main/java/slate/app/ota/NordicLegacyDfuClient.kt` already treats link-loss during ACTIVATE as success (`resetDuringActivate`), but operators still see "failed" toasts when the watch resets at other legitimate points after the image is fully transferred (GATT status 8, supervision timeout — exactly what `LinkLostException`'s comment describes). Tighten the classification and add post-DFU verification:
>
> 1. Introduce an explicit transfer phase state (`CONNECTING / TRANSFERRING / IMAGE_COMPLETE / ACTIVATING`) in the client. Any `LinkLostException` at `IMAGE_COMPLETE` or later is *not* a failure: report "image transferred; watch is swapping — verify on the watch" instead of an error. Before `IMAGE_COMPLETE`, keep failing hard. Keep an explicit DFU error-response opcode as failure in every phase (the existing `OP_VALIDATE`/response path).
> 2. Add a post-DFU verification step in `SealedDfuService.kt`: after the link drops post-`IMAGE_COMPLETE`, scan for up to ~90 s for the device readvertising — either Slate's service UUID or an InfiniTime advertiser — and upgrade the final notification to "watch rebooted and is advertising (Slate/InfiniTime)". MCUBoot swaps take tens of seconds on this hardware; pick the timeout generously and surface progress.
> 3. Reword the failure copy that remains: it must tell operators to check the watch face before trusting the phone (mirrors the note in `issues.md` I-7).
> 4. Unit-test the phase classification with the existing event-channel fakes (the client is already structured around an `Event` channel — simulate disconnects at each phase).
>
> Acceptance: a transfer that completes and then loses the link never shows a red failure state; genuine mid-transfer drops still do.

---

## I-8 — Harden sealed-DFU target classification

> `SealedDfuPreflight.classify` (used by `SealedDfuProbeActivity.kt`) is a heuristic: MCUBoot has no BLE, so the phone cannot prove the bootloader layout. The residual risk is accepted, but narrow it:
>
> 1. **Positive identification before send:** when connected to the target, require the Nordic legacy DFU service (InfiniTime `DfuService` UUID `00001530-1212-EFDE-1523-785FEABCD123`) and read DIS (Device Information Service) strings if present. Classify: InfiniTime (DIS match or adv name `InfiniTime`), InfiniTime-recovery, unknown-legacy-DFU. Only InfiniTime/recovery proceed normally; "unknown" requires a typed confirmation showing the device fingerprint (name, address, DIS strings, advertised services).
> 2. **Package-side checks stay strict:** keep `NordicDfuPackage.kt` rejecting non-`0x0052` device types, SoftDevice/bootloader payloads, and non-MCUBoot images (`slate-mcuboot-image.bin` with MCUBoot header magic). Add a check that the image's MCUBoot header `img_size` fits the InfiniTime primary-slot contract (475136 minus header/trailer) so an oversized image can't be sent toward a smaller-slot custom bootloader.
> 3. Document the remaining irreducible risk in `docs/flash-sealed.md`: a custom bootloader exposing the same DFU service with a different secondary map can still be bricked; the mitigation is the typed-confirmation gate.
> 4. Add unit tests for the classifier verdicts including the new "unknown → confirm" path.

---

## I-9 — InfiniTime low-level divergence audit

> Standing policy (`CLAUDE.md`): mirror InfiniTime for boot, DFU, flash map, WDT/button, BLE bring-up; differ only at the SDP layer. Produce `docs/infinitime-parity.md`: a table auditing every low-level behaviour against InfiniTime `main`, with columns {area, InfiniTime behaviour + file ref, Slate behaviour + file ref, verdict: aligned / mirror-gap (fix Slate to match InfiniTime — the default for anything low-level) / documented exception (allowed only with a sealed-watch rationale recorded in the table)}. Cover at minimum:
>
> - **Advertising:** InfiniTime `NimbleController::StartAdvertising()`: fast adv itvl 32–47 (0.625 ms units, ~20–29 ms) for ~30 s (`fastAdvCount < 15` × 2000 ms adv bursts), then slow 1636–1651 (~1.02 s); device name in the **scan response**, not adv data; adv payload carries HRS uuid16 + DFU uuid128; appearance 0xC2 (watch); adv restarts on disconnect, on failed connect (`event->connect.status != 0`), and `RestartFastAdv` at wake. Slate `src/ble_nimble.cpp` advertises `BLE_HS_FOREVER` with default intervals — adopt InfiniTime's fast/slow cadence, scan-response name placement and restart rules outright; advertising is low-level bring-up, so the verdict is *mirror*, not *decide* (it is also battery-relevant).
> - **WDT:** InfiniTime `watchdog.Setup(7, SleepBehaviour::Run, HaltBehaviour::Pause)`; reload gated on button-released at ~100 ms cadence in `SystemTask::Work`, vs Slate's tick-hook pet at 1024 Hz — verify Slate's CONFIG sleep/halt bits match and note the cadence difference.
> - **Button:** enable pin driven high permanently; sense GPIOTE toggle with pulldown; release detected via `nrf_gpio_pin_read == 0`.
> - **Boot/confirm:** InfiniTime `FirmwareValidator` writes the IMAGE_OK **word** as 1 (Slate `boot_util.cpp` already matches — keep the rationale comment); manual Validate UX vs Slate's 10 s dwell (the one documented exception; link I-6).
> - **Power:** HRS3300 disabled at boot (`heartRateSensor.Init(); Disable()`), SPI + SpiNorFlash sleep when the display sleeps (guarded by `BootloaderVersion::IsValid()` — old bootloaders can't re-init sleeping NOR flash: check Slate's XT25 sleep path against this brick vector), touch panel sleep except double-tap wake, ChargingEvent → GoToRunning + measure.
> - **Battery:** measurement timing and curve (link I-5).
> - **CTS/time:** InfiniTime runs a CTS client + server with service discovery deferred ~3 s after connect (link I-12).
> - **DFU service behaviour** during transfer: `BleFirmwareUpdateStarted` → wake lock; validated → `NVIC_SystemReset()`.
>
> For each mirror-gap verdict, file a follow-up entry in `issues.md`. Do not change code in this task.

---

## I-10 — FreeRTOS task topology split (design doc)

> Roadmap §3.1's `display` / `link` / `sensors` / `system` split is deferred; today one 768-word `app` task (prio idle+2, `src/main.cpp`) does UI, WDT, session and core tick, beside NimBLE ll/host and the timer daemon. Write `docs/task-topology.md` — a design + migration plan, no code:
>
> 1. Reference topology: InfiniTime runs MAIN/SystemTask (350 words, prio 1, 10-deep byte queue), a DisplayApp task, HeartRateTask, and NimBLE tasks; ISR-safe messaging via `PushMessage` choosing `xQueueSend` vs `xQueueSendFromISR` on `SCB->ICSR VECTACTIVE`. Adopt the same queue-of-enum message pattern per task.
> 2. Constraints: ≤54 KiB static+heap with ≥6 KiB slack (CI gate), `configTOTAL_HEAP_SIZE` 16 KiB already tight (NimBLE eventq history in `config/FreeRTOSConfig.h` comments), every task documents its stack and high-water marks are checked in debug (`uxTaskGetStackHighWaterMark` is enabled).
> 3. Deliverables in the doc: proposed task set with priorities/stack budgets summing inside the RAM gate; which current `app_loop` responsibilities move where — the WDT pet must stay on a path that survives any single wedged task (it currently lives in the tick + idle hooks, which is exactly that property; say why it stays); queue depths and message enums; migration order (suggest: extract `link` first since session/BLE callbacks already marshal, then `display`); rollback plan per stage (each stage must boot on sealed hardware and pass the `freertos_smoke` proof).
> 4. Explicitly list what does NOT change: WDT withhold semantics, RTC1 tick ownership, NimBLE task config.

---

## I-11 — HRS3300 driver + Heart Rate Service (behind the existing flag)

> `include/ble_gatt.hpp` has `heart_rate = false` pending an HRS3300 driver. Implement the driver + GATT plumbing, keeping it flag-off by default:
>
> 1. **Driver** (`src/hrs3300.cpp`, C++ class per repo conventions, no dynamic alloc after init): I2C addr 0x44, registers per InfiniTime `drivers/Hrs3300.cpp` — ENABLE (0x01), PDRIVER (0x0C), RES (0x16), HGAIN (0x17); init sequence then immediate `Disable()` (PDRIVER 0x00), exactly like InfiniTime's boot (`heartRateSensor.Init(); heartRateSensor.Disable();`). The chip powers on enabled — `power.cpp hrs_sleep()` already sleeps it at boot; make the driver the single owner of that responsibility and redirect the ad-hoc sleep. All TWI ops through `twi::` with timeouts (bus rule from `CLAUDE.md`).
> 2. **Measurement path:** defer the PPG algorithm — expose raw ALS/HRS readings and a stub `bpm()` returning invalid; document that InfiniTime's `Ppg.cpp` DC/AGC algorithm is the follow-up.
> 3. **GATT:** standard HRS 0x180D with Heart Rate Measurement 0x2A37 notify, gated by the existing `heart_rate` flag in `ble_gatt.cpp`; advertise the uuid16 only when the flag is on (InfiniTime puts the HRS uuid16 in adv data). Respect `SLATE_BLE_MAX_CONNECTIONS 1` and the mbuf budgets in `docs/ble.md`.
> 4. **Tests:** host-side unit test for the register sequences (mock TWI) and for the GATT payload encoding (flags byte + u8 bpm).
> 5. RAM gate: driver + service must fit inside the 54 KiB budget — report the delta from the link map.
>
> Acceptance: flag off → binary behaviour unchanged except HRS sleep ownership; flag on → service enumerable, notifications carry stub values; sensor asleep whenever the flag is off or nobody subscribes.

---

## I-12 — Current Time Service (CTS) for non-companion time sync

> Time sync today is companion-only: CONTROL op `0x20` + u32 LE epoch (`companion/sdp-core/.../TimeSync.kt` ↔ `src/main.cpp`). Add standard GATT CTS so Gadgetbridge/anything can set time, mirroring InfiniTime:
>
> 1. Implement a **CTS server** (service 0x1805, Current Time char 0x2A2B read/write) in `src/ble_gatt.cpp` — InfiniTime's `CurrentTimeService` accepts the exact-time-256 write (year u16 LE, month, day, hour, min, sec, day-of-week, fractions256, adjust reason) and sets its DateTime controller. Map writes onto `wall_clock` the same way CONTROL 0x20 does; validate all lengths/fields and reject-and-resync per the BLE parser rule in `CLAUDE.md`.
> 2. Optionally (second stage, InfiniTime parity): a **CTS client** that queries the central's CTS after connect, with discovery deferred ~3 s like InfiniTime's `bleDiscoveryTimer`, to avoid host/target discovery collisions. Note in code why the delay exists.
> 3. Precedence rule: companion CONTROL 0x20 wins over CTS when both fire (last-writer-wins is acceptable; document it). Keep the wire constant `0x20` untouched in `include/sdp_opcodes.hpp`.
> 4. Host-side tests: valid write sets clock, short/oversized writes rejected, month/day bounds enforced.
> 5. Verify RAM delta against the CI gate.

---

## I-13 — Prove channel-5 SDP OTA end-to-end on hardware

> `ota_xfer` (chunked, resumable, SHA-256 verified, 30 % battery gate — `include/ota_xfer.hpp`) exists but has never completed on a durable, confirmed Slate install. After I-1/I-2 clear, run and document the proof. Prepare everything now:
>
> 1. Write `docs/ota-verification.md`: the test matrix — happy path (full transfer → MCUBoot swap → amber → confirm), resume after link drop at 25/50/75 %, resume after phone app kill, battery-gate refusal below 30 % and "unknown battery" refusal (link I-5), SHA mismatch rejection, and the `IMAGE_OK` interplay (OTA must refuse to start while the *current* image is still unconfirmed — verify this guard exists in `ota_xfer.cpp`/`ota_slot.cpp`; add it if missing).
> 2. Add instrumentation so failures are diagnosable on sealed hardware: transfer progress + last error surfaced through the diag overlay (`local_ui.cpp` diag line) and RTT; companion-side per-chunk log with offsets.
> 3. Host-side: extend the existing desktop tests with a full simulated transfer including forced resumption points and a corrupted-chunk case, so the on-watch run only validates radio + flash behaviour, not protocol logic.
> 4. Record results per matrix row in the doc; any failure becomes a new issue in `issues.md`.
>
> Preferred after I-1/I-2; do not attempt on the soft-bricked unit.

---

## I-14 — RAM headroom watch (~93 % of prod link map)

> The hard gate is static+heap ≤ 54 KiB with ≥6 KiB slack; heap history shows NimBLE eventq starvation turning into sealed-watch cyan asserts (`queue.c:1513`, see `config/FreeRTOSConfig.h` comments). Make regressions visible before they brick anything:
>
> 1. **CI delta reporting:** extend the RAM gate (`SLATE_RAM_LENGTH 0x10000` in `CMakeLists.txt` plus whatever script enforces the 54 KiB rule — locate it first) to parse the prod link map and emit a per-section breakdown (`.data`, `.bss`, heap, per-task stacks, NimBLE pools) with a stored baseline; fail CI on >512 B unexplained growth even inside the gate, warn at slack <8 KiB, fail at <6 KiB.
> 2. **Runtime watermarks:** in debug builds, log `xPortGetMinimumEverFreeHeapSize()` and each task's `uxTaskGetStackHighWaterMark()` periodically via RTT, and expose worst-case values in the diag overlay so sealed soaks capture them (follow the `diag_stall_ms` pattern in `local_ui.cpp`).
> 3. **Malloc-failure path:** `vApplicationMallocFailedHook` paints red via `boot_diag::fatal_paint_and_hang` — confirm the hang loop lets the bootloader WDT expire (pet withheld) so a sealed watch reverts/reboots with the colour visible rather than hanging forever; align semantics with `wdt::fatal_starve`.
> 4. Document the NimBLE heap sizing rationale (16 KiB, bisect 18 KiB) in `docs/ble.md` where the mbuf math already lives.

---

## I-15 — Companion UX when the watch is not advertising

> CDM associate / "Start reconnect" spin forever against a silent radio (soft-brick, or watch on InfiniTime while the companion expects Slate). Make the companion diagnose instead of confuse:
>
> 1. In the associate/reconnect flows (`AssociationHelper.kt`, `LinkForegroundService.kt`), add a bounded scan phase: if the target address (or any Slate-service advertiser) isn't seen within ~30 s, stop and show a state page: "Watch not advertising as Slate" with three branches — (a) watch on InfiniTime → detected when an `InfiniTime`-named / DFU-service advertiser is seen → offer the sealed-DFU flow directly (`SealedDfuProbeActivity`); (b) another app may hold the connection → run the I-6/I-16 occupancy check; (c) nothing heard at all → link to the I-1 recovery runbook (bootloader blue/red), with the discharge caveat.
> 2. `startObservingDevicePresence` (API 31+) stays for background reconnect, but foreground flows must not rely on it for feedback — CDM presence callbacks are silent when the radio is dead; say so in a code comment.
> 3. The sealed-installer entry point should reuse the same scan classifier so "Select InfiniTime / recovery (CDM)" pre-checks that an InfiniTime-ish advertiser exists before opening the CDM chooser.
> 4. Tests: unit-test the classifier on synthesized scan results (Slate adv, InfiniTime adv, silence).

---

## I-16 — Multi-app BLE contention (Gadgetbridge et al.)

> `SLATE_BLE_MAX_CONNECTIONS 1` (`include/slate_nimble_cfg.hpp`): any other central blocks HELLO, confirm, OTA and DFU. Build detection + guidance instead of relying on folklore:
>
> 1. **Occupancy detector** (shared with I-6 step 3 and I-15): `BluetoothManager.getConnectedDevices(BluetoothProfile.GATT)` filtered to the watch address tells you when *this phone* holds the link via another app; a watch that is bonded-but-silent while not advertising suggests a foreign central. Wrap both signals into one `LinkContention` check with a human-readable verdict.
> 2. Run the check before: associate, reconnect, sealed DFU, OTA, and the confirm countdown. On contention, show app-specific remediation: Gadgetbridge → disable auto-reconnect or remove device; Amazfish → disconnect; nRF Connect → disconnect tab. Keep the copy in one place.
> 3. **Firmware:** on `BLE_GAP_EVENT_DISCONNECT` Slate resumes advertising (`src/ble_nimble.cpp` "Resume advertising for the next central") — verify it also resumes after a *failed* connect attempt (InfiniTime restarts adv on `event->connect.status != 0` in `OnGAPEvent`) so a foreign central's failed attempt can't leave the radio silent; add the case if missing, with a host-side test if the GAP event path is testable on desktop.
> 4. Document the one-slot reality prominently in the companion's troubleshooting screen and cross-link `docs/flash-sealed.md` step 7.
>
> Acceptance: every blocked flow names the blocker within seconds instead of timing out silently.

---

# N-series — new issues found in a full-repo audit (not in issues.md)

From reading `sdp_frame.cpp`, `sdp_parser.cpp`, `session.cpp`, `main.cpp`,
`ble_nimble.cpp`, `ble_link.cpp`, `spi_bus.cpp`, `twi.cpp`, the renderer, and the
companion (`SlateGattClient`, `AndroidJsEngine`, `ScriptRuntimeHost`, `RepoManager`,
`RepoHttp`, `Compositor`). All fixes below preserve the core concept: the phone
pushes display lists, the watch renders them and returns element-level input
events, with a resilient local core. Positive audit notes first: the SDP parser's
per-opcode validation is genuinely thorough (reject-and-resync, palette/coord/enum
checks, skippable 0xE0–0xEF extensions); the repo pipeline verifies Ed25519 index
signatures + per-package SHA-256; the JS engine fails closed when the 4 MB isolate
heap cap is unsupported; golden-file tests exist and run (91 tests in sdp-tests).

---

## N-1 — Display lists are parsed AND rendered on the NimBLE host task (data race + stack risk)

> **Status: resolved** — `ble::AppInbox` (1×4 KiB) + `Link::drain_app_messages` in
> `app_loop`; host test `ble_app_inbox`. See `issues.md` N-1.

> **Severity: critical — this is a correctness bug in the thin-client hot path, not deferred design.** Trace: NimBLE host task executes `rx_access` (`src/ble_nimble.cpp`) → `Link::on_rx_write` (`src/ble_link.cpp`) → `on_app_message` (`src/main.cpp`) → `session::Manager::on_display` → `apply_list` → `g_interp.push_list` — i.e. the full SDP parse, tile render, and SPI DMA to the ST7789 run on the **NimBLE host task**, synchronously inside a GATT write callback. Meanwhile the `app` task concurrently calls `g_core.show_current()` → `core_push_list` → the **same** `g_interp`/`g_renderer`, and `g_input.set_hits(...)` swaps the hit-rect array under the input poller. Shared mutable state with no lock: interpreter, renderer tiles + dirty tracking, `g_session` fields, `g_local_owns_screen`, hit rects. Consequences: torn renders/hit-tests, and a full-screen redraw (~115 ms at 8 MHz) blocking the BLE host task — delaying connection events, heartbeats and credits that Slate itself depends on. The NimBLE host stack also wasn't sized for a render path.
>
> Fix with the InfiniTime pattern (queue to the owning task; their GATT callbacks only `PushMessage` to `SystemTask`/`DisplayApp`):
>
> 1. In `on_app_message` (or better, inside `Link`), stop calling session/interpreter directly from the host task. Copy the reassembled message into a queue slot and `xQueueSend` a descriptor to the app task; the app task drains the queue in `app_loop` and performs session dispatch + parse + render there. Note the current message pointer aliases the `Reassembler`'s internal `buf_`, so the copy is mandatory once processing is deferred.
> 2. RAM budget the queue against the 54 KiB gate: `sdp::frame::kMaxMessageBytes` is 4096, so a naive 2-deep queue of full messages costs 8 KiB — too much. Options: single 4 KiB slot + drop-and-count when busy (SDP has credits — withhold the credit until the slot frees, so a well-behaved companion never overruns), or two slots of 2 KiB with large-message spill. Justify the choice in a comment and report the measured RAM delta.
> 3. CONTROL/INPUT/system messages are small — either the same queue with a small-copy fast path, or a second shallow queue. Keep ordering guarantees per channel.
> 4. Credits/backpressure: `send_credit()` currently fires from the host task after apply; after the move it must fire from the app task after the *deferred* apply completes, which is also more honest flow control.
> 5. Add a host-side test simulating concurrent host-task ingest + app-task local renders to prove the interpreter is only ever driven from one context.
>
> This partially front-runs the I-10 task split — implement it as "the `link`→`app` handoff", the first stage of that roadmap, rather than a throwaway.

---

## N-2 — Watchdog cannot catch a wedged app task (pet lives in ISR context)

> **Severity: high, sealed-watch critical.** `slate::wdt::pet()` is called from `vApplicationTickHook` and `vApplicationIdleHook` (`src/main.cpp`). Both keep running when the `app` task deadlocks (e.g. stuck in a busy-wait — see N-5), so the bootloader WDT stays fed and a wedged watch **never resets on its own**; only a 7–8 s button hold (or a flat battery) recovers it. InfiniTime reloads the WDT from inside `SystemTask::Work()`'s ~100 ms state-update loop — a wedged main task there starves the dog and reboots within 7 s. WDT behaviour is squarely low-level, so mirror that outright rather than inventing a Slate-specific mechanism:
>
> 1. Move the reload into the app task: call `wdt::pet()` (still gated on button-released) once per `app_loop` iteration in `src/main.cpp` — the loop runs every 20 ms (200 ms ambient), the direct analogue of InfiniTime's 100 ms cadence against the same 7 s bootloader dog.
> 2. Remove the pet from `vApplicationTickHook` and `vApplicationIdleHook`. A wedged app task then starves the WDT and the bootloader resets within ~7 s, exactly like InfiniTime. Keep the beat/stall *telemetry* in the tick hook (`g_beat_stall_ticks` / `g_max_stall_ms`) — it stays useful for `diag_stall_ms` — but it no longer touches the WDT.
> 3. Keep `configPOST_SLEEP_PROCESSING` → `slate_wdt_pet` for the future tickless path (I-3): sleeps are capped at 1 s precisely so the dog is fed across them while the app task is blocked in `vTaskDelay`.
> 4. Audit app-task operations that could legitimately block the loop for a substantial fraction of 7 s — XT25 block erases in `ota_xfer`/`asset_xfer`, LittleFS GC, long SPI pushes — and either bound them per iteration or insert explicit pets inside them (InfiniTime does the same implicitly by keeping such work off SystemTask). Document the worst-case loop stall next to the pet call.
> 5. Update the CLAUDE.md concrete example and the `issues.md` R-3 note: the tick path no longer pets at all, which satisfies "do not 'always pet' from the tick ISR" by construction; the button-hold withhold in `wdt::pet()` is unchanged.
> 6. Host-side test: fake loop iterations petting normally; simulate a wedge (no iterations) and assert no reload happens from any hook; simulate button-held and assert reload is withheld even while the loop runs.
>
> Record the outcome in the I-9 parity table as *aligned*.

## N-3 — `now_ms()` wraps at ~71.6 minutes and breaks session timing

> **Severity: high, user-visible.** `static std::uint32_t now_ms() { return board::micros() / 1000u; }` (`src/main.cpp`), and `board::micros()` is `std::uint32_t` (TIMER1). `micros()` wraps mod 2³², so `now_ms` jumps from 4,294,967 to 0 every ~71.6 min — and because the wrap point isn't 2³², unsigned subtraction like `now_ms - last_hb_ms_` is **not** modular-consistent across it: one sample after the wrap yields an elapsed of ~4.29 billion ms. Consumers: `session::Manager::tick` heartbeat logic (5 × 2 s misses → `pop_remote_all(TIMEOUT)` — the remote screen spuriously pops to the local face mid-session, a direct thin-client UX break), `stale` marking, `boot::tick_confirm`'s dwell timer, `Core::tick` cadences, and the 60 s battery resample. Roughly once every 71.6 minutes of uptime, at least one of these misfires.
>
> 1. Replace `now_ms` with a monotonic 64-bit source: under FreeRTOS derive from `xTaskGetTickCount()` (1024 Hz, 32-bit → still wraps at ~48.5 days; use `rtc_hw`'s overflow-extended tick counter that `wall_clock` already maintains for a true u64), falling back to a u32-extension of `micros()` for bare-metal builds. Keep the return type u32 ms *only if* you make the wrap point 2³² so modular subtraction works — otherwise return u64 and let call sites keep u32 deltas.
> 2. Audit every `now_ms`/elapsed subtraction in `main.cpp`, `session.cpp`, `local_core.cpp`, `boot_util.cpp` for wrap assumptions; `button.cpp` uses `micros()` deltas cast through `int32_t`, which *is* mod-2³²-safe — leave it but note why.
> 3. Host-side regression test: drive `session::Manager::tick` across the old wrap boundary (e.g. last_hb at 4,294,000 ms, now at 500 ms) and assert no spurious heartbeat pop; same for `tick_confirm`.
> 4. While in there: `on_app_message` decodes CONTROL op `0x20` with a raw literal in `main.cpp` — the wire-constant rule says it belongs in `include/sdp_opcodes.hpp` (`control_op::TIME_SYNC`); fix the hygiene without changing the wire value.

---

## N-4 — Frame reassembler: per-channel state but ONE shared 4 KiB buffer

> **Severity: medium (latent corruption).** `sdp::frame::Reassembler` (`include/sdp_frame.hpp`, `src/sdp_frame.cpp`) keeps `ChanState ch_[8]` per channel but a single `buf_[kMaxMessageBytes]`. Two interleaved multi-fragment messages on different channels — entirely legal-looking given the per-channel sequence tracking, and realistic once OTA chunks (channel 5) interleave with DISPLAY lists (channel 1) or asset transfers (channel 4) — write into the same buffer and silently corrupt each other. Today it "works" because the companion serializes GATT writes message-by-message, but nothing on the watch enforces that, and a malicious or buggy central must not be able to corrupt a display list mid-reassembly (BLE-facing parser rule in `CLAUDE.md`).
>
> Choose and implement one, with the RAM gate in mind:
>
> 1. **Enforce single-in-flight (recommended, zero RAM):** when a FIRST fragment arrives on channel B while channel A has `active` state, treat it as protocol violation — reset the *older* channel's state and count a drop (reject-and-resync, never fault). Document in the §4.2 framing spec section of the roadmap doc that multi-fragment messages must not interleave across channels, and add the matching rule to the companion encoder (`fragment_message` callers / the Kotlin frame writer in `sdp-core`) plus a golden test proving the encoder never interleaves.
> 2. **Or per-channel buffers** for the channels that legitimately stream large messages (DISPLAY, ASSET, OTA) — only if the RAM budget allows; smaller channels (CONTROL, INPUT) can share a slot since their messages are single-fragment.
>
> Either way: extend `tests/host/test_sdp_frame.cpp` with interleaved-channel cases (A-first, B-first, B completes first, B aborts A, reserved-channel mixed in), asserting no cross-contamination of delivered messages. Keep `FrameStatus` semantics and the frozen wire format unchanged.

---

## N-5 — SPI transmit busy-waits forever; TWI recovers but SPIM cannot

> **Severity: medium (freeze vector, compounds N-2).** `spi::transmit`/`transfer` (`src/spi_bus.cpp`) spin on `while (EVENTS_END == 0) {}` with no timeout. A wedged SPIM transaction (peripheral disabled mid-flight by a `power::buses_idle` race, errata, or EMI) hangs the calling task inside the bus mutex — display, external flash (LittleFS), OTA writes all stop, and with today's WDT behaviour (N-2) the watch freezes forever instead of resetting. Contrast `src/twi.cpp`, which has `wait_event(timeout)` + `recover_bus()` — the CST816S sleep behaviour forced good hygiene there; give SPI the same.
>
> 1. Add a bounded wait (µs-scale via `board::micros()`; a 255-byte chunk at 8 MHz is ~255 µs, so a 5 ms cap is generous) to both EVENTS_END loops.
> 2. On timeout: stop/disable/re-enable SPIM0, reassert pin config, deassert both CS lines (display **and** flash — the shared-bus rule says exactly one CS asserted, and a wedged transfer may leave one low), bump a diag counter surfaced like the mbuf stats, and return an error.
> 3. Propagate failure: `transmit`/`transfer` become `bool`; audit callers (`st7789.cpp`, `xt25.cpp`, `glyph_cache`, LittleFS hooks) to fail their operation cleanly rather than render garbage — a failed tile push should mark the rect dirty for retry, a failed flash op should return an LFS error.
> 4. While in the file: the CS-discipline and non-reentrancy checks are plain `assert(...)`, compiled out under Release (`-Os`; CMake Release defines NDEBUG). Replace with an always-on lightweight check that feeds `boot_diag`/RTT in release builds, since a CS violation on the shared display+flash bus is precisely a sealed-watch failure you'd want reported (ties into I-14's diag work).
> 5. Host tests where feasible (the register block is accessed via `nrf::reg` — mockable); at minimum, unit-test the timeout math and the caller error paths.
> 6. Policy note: the fully mirrored alternative is adopting InfiniTime's IRQ-driven `SpiMaster` structure wholesale. The bounded-wait fix above is the proportionate step now; if this driver keeps diverging in behaviour, escalate to the full mirror and record it in the I-9 table.

---

## N-6 — HELLO_OFFER encoder has unguarded buffer writes

> **Severity: low (robustness).** `session::Manager::encode_hello_offer` (`src/session.cpp`) builds into `std::uint8_t tmp[160]`; the `push` lambda bounds-checks, but the `put_u16(tmp + n, ...); n += 2;` pairs and `fill_opcode_bitmap(tmp + n); n += 32;` do **not**. Today the payload fits; grow `profile::kCatalog` (names up to `kMaxProfileNameLen`, 4 fixed bytes each) or the bitmap and this becomes a silent stack-buffer overflow in the watch's *outbound* hello — not attacker-controlled, but exactly the class of bug the project style forbids.
>
> 1. Make every write go through capacity-checked helpers (extend the `push` pattern with `push_u16` / `push_bytes` that set an `ok` flag), return 0 on overflow like the local-UI writer `W` does.
> 2. Add a `static_assert` (or host test) computing the worst-case encoded size from `profile::kCatalogCount × (4 + kMaxProfileNameLen)` + fixed fields against the buffer size, so catalog growth fails the build, not the watch.
> 3. Host test: encode with the current catalog, assert byte-exact golden (this doubles as companion-side decoder coverage — the Kotlin session parser should get the same fixture).

---

## N-7 — Companion hardening niggles (grouped)

> Three small companion items, one prompt, no behaviour changes to the thin-client contract:
>
> 1. **`ScriptRuntimeHost.HOST_HELD = ScriptPermission.entries.toSet()`** — the host asserts it holds *every* permission, so effective sub-app gating rests solely on `installed.permissions`. That works, but it means a future permission added for a privileged internal app is silently grantable to store apps too. Split into `HOST_HELD` (what the host can technically provide) vs a per-source ceiling (bundled/official/third-party), enforced in `JsSlateAppEndpoint` binding setup; log at registration which permissions were requested vs granted. Keep the CLAUDE.md rule intact: whitelisted bindings only, no reflection/filesystem/raw Android APIs.
> 2. **`RepoHttp` redirects:** `instanceFollowRedirects = true` with a post-hoc `conn.url.protocol != "https"` check is correct for `HttpURLConnection` (it refuses cross-protocol redirects anyway), but add a redirect-count/self-host policy note and a unit test with a mock server so the downgrade guard doesn't silently rot if the HTTP stack is ever swapped.
> 3. **DFU/OTA battery gate vs unknown battery** (companion side of I-5): `NordicLegacyDfuClient.checkBatteryIfAvailable` treats a missing Battery Service as "fine, proceed". After the firmware exposes an explicit invalid-battery state, prefer blocking sealed DFU when the level is unknown *and* the watch reports not-charging, with an override prompt — a mid-swap brownout is a brick on a sealed watch.

---

## Suggested execution order (updated)

I-1 → I-2 → **N-2, N-3, N-5** (small, high-value, safe-to-flash fixes that ride along with the I-2 rebuild) → I-6/I-16 → I-4 → I-5 (+ N-7.3) → **N-1** (link→app handoff; first stage of I-10) → **N-4, N-6** → I-7/I-8 → I-3 (soak) → I-13 → I-9/I-14 ongoing → I-10 (rest)/I-11/I-12 once bring-up is stable.
