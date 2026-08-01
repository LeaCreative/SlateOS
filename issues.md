# Open issues — Slate / EvoTime

Last updated: 2026-07-31

Standing rule: **mirror InfiniTime** for boot, MCUBoot/DFU, flash map, WDT/button
reset, and BLE bring-up
([InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime)). Differ on purpose only
at the SDP / companion JS display-list layer.

---

## Active / blocking

### I-1 — Sealed watch soft-brick (no BLE, no InfiniTime)

- **Status:** **Resolved** — watch recovered and running a confirmed Slate
  image; runbook and regression fence in tree, button-hold reset proven
- **Area:** Firmware / sealed DFU
- **Impact:** Watch unusable over the air until recovered
- **Notes:** After RTC-tick DFU (`slate-dfu.zip` SHA prefix `790822E97A78`; earlier
  tickless package `CC3F25A12AB2` also regressed), device shows Slate’s tiny
  glyphs, does not advertise, and InfiniTime does not return. Procedure:
  [`docs/recovery-sealed.md`](docs/recovery-sealed.md). In-tree WDT withhold
  mirrors InfiniTime; host test `wdt_hold` fences RR0 reload while held. User
  still needs discharge → pinecone → InfiniTime → fixed Slate flash (I-2).

### I-2 — Fixed Slate DFU package (post-recovery flash)

- **Status:** **Resolved** — flashed and confirmed on 31 July; the amber trial
  bar cleared, so `IMAGE_OK` is written and the install survives reset. This
  entry now just tracks the current staged package.
- **Area:** Firmware packaging
- **Artifact:** `build/dfu/slate-dfu.zip`
  - **SHA-256:** `B7A58CFAC9B6CBC61DA55CD7D7C5D801337C374623B94106644DB76EECD9E28E`
  - **SHA-256 prefix (12):** `B7A58CFAC9B6`
  - Built 2026-07-31 (night): `BOOTLOADER_PRESENT=ON`, `SLATE_HAS_NIMBLE=ON`,
    `imgtool create --slot-size 475136` (InfiniTime contract, unsigned)
  - Built 2026-08-01: everything verified to date (N-1/N-4/N-6/N-8, N-9
    identity, N-10/N-11 tick, N-12 battery) plus the **N-13 fix**: app task
    at BLE-host priority, repaint only on change, time-based diag cadence
  - Link map: FLASH ~122104 B / 475104 B; RAM ~60952 B / 64 KB
    (`__heap_end__` ~488 B below `__StackLimit`)
  - Supersedes `E54859689936` (N-12 verified, N-13 diagnosed from it),
    `C3AE3F15E408` (first confirmed durable install), and the earlier
    bring-up images `E95E6CD9676B`, `4324FC495E0C`, `2284BA304B8F`,
    `D7E86854B9D3`, `8B2D9054E9E7`
- **Fix asserts (all pass):**
  - (a) `src/wdt.cpp` — `pet()` returns before RR0 write when `button_raw()`
  - (b) `button.cpp` “Enable stays high” + `board::button_hw_init` OUTSET enable
  - (c) `port_rtc_tick.c` — one `xTaskIncrementTick` per TICK; no ISR catch-up
  - (d) `configUSE_TICKLESS_IDLE 0`
- **Flash order:** recovery/InfiniTime first ([`docs/recovery-sealed.md`](docs/recovery-sealed.md)),
  then this zip per [`docs/flash-sealed.md`](docs/flash-sealed.md). Confirm:
  amber bar → any central ~10 s (`kConfirmDwellMs` = 10000) → `IMAGE_OK`.
- **Do not flash:** `CC3F25A12AB2`, `790822E97A78`

### N-8 — Firmware no longer links: RAM heap/stack collision since N-1

- **Status:** Resolved (zero-copy `AppInbox` — borrows the reassembler buffer)
- **Area:** Linker / RAM budget (`ble::AppInbox`)
- **Impact:** Was: `build/dfu` failed `ASSERT(__StackLimit >= __heap_end__)`
  (`__heap_end__` 65040 B vs `__StackLimit` 61440 B — 3,600 B over). The
  I-2 zip had linked with only ~8 B margin; N-1's 4 KiB copy slot pushed
  heap past the 4 KiB MSP stack reserve, so no ARM image linked since.
- **Notes:** `AppInbox` no longer stores a 4 KiB copy: it borrows the
  reassembler's buffer ({ptr, channel, len} ≈ 12 B) and `Link::on_rx_write`
  gates ALL ingest (incl. DIAG) while a message is pending — fragments in
  that window are dropped and counted (`note_busy_drop`); CREDIT withheld
  until apply keeps well-behaved companions out of it. N-1 semantics
  unchanged (host never dispatches; app task drains). Both `build/dfu` and
  `build/dfu-prod` link again: RAM 60944 B / 64 KB (92.99 %),
  `__heap_end__` 0x2000EE10 → 496 B below `__StackLimit`. Host:
  `test_ble_app_inbox` (incl. new busy-window resync case).

### N-12 — Battery always reads 0 %: SAADC gain and resolution mis-set

- **Status:** **Resolved — verified on hardware.** Overlay read `831/3895`:
  831 × 4800/1024 = 3895 mV, a plausible cell voltage, and the displayed
  percentage tracks it. Multimeter cross-check still worth doing once (I-5).
- **Area:** `power.cpp::sample_battery_adc`, `battery.cpp::millivolts`
- **Impact:** Battery shows `0` (a *valid* 0, not `--`) on a charged cell.
  Also blocks I-13: SDP OTA and sealed DFU both gate on ≥30 % battery.
- **Root cause:** two independent register bugs.
  1. `CH0_CONFIG` GAIN field held **5**, which is gain **1**, not the 1/6 the
     comment claimed (encoding: 0=1/6 … 5=1). With the internal 0.6 V
     reference that puts full scale at 0.6 V while the divided battery pin
     sits near 2.0 V — every sample rails.
  2. `RESOLUTION` was set from a constant named `RES_12BIT` whose value was
     **1**, which is 10-bit (encoding: 0=8, 1=10, 2=12, 3=14). The name was
     wrong by 4x and silently agreed with nothing.
  The `adc * 2000 / 1241` conversion matched neither configuration, so the
  railed count converted to ~1.6 V and `percent_from_mv` floored it at 0.
- **Fix (mirror InfiniTime):** gain 1/4 + internal reference (full scale
  2.4 V on the pin = 4.8 V of battery through the 1:2 divider), 10-bit, and
  InfiniTime's `mV = raw * 8 * 600 / 1024`. Constants renamed so the
  encodings are written down. Host `test_battery` inverts the new formula.
- **Verify:** the diag overlay's new second line ends `/<adc raw>/<mV>`. A
  healthy cell should land ~3700–4200 mV; compare against a multimeter
  before trusting the curve breakpoints (the open item in I-5).

### N-17 — Slate service UUID published byte-scrambled

- **Status:** Fix staged (`B7A58CFAC9B6`). **First attempt `FAE65071E5A7`
  changed nothing on the wire** — `include/slate_uuids.hpp` is not what the
  radio uses. `src/ble_nimble.cpp` carried its own hardcoded
  `BLE_UUID128_INIT` bytes (line ~311), so fixing the header alone was
  invisible. Both are now corrected and bound together by `static_assert`,
  and the built binary is verified to contain the corrected bytes and none
  of the old ones.
- **Duplicate-definition tell:** in the *same file*, `uuid_dfu_*` was written
  as a correct full reversal while `uuid_svc`/`rx`/`tx`/`status` used the
  mixed-endian layout. That is exactly why the Nordic DFU service always
  worked and Slate's own service never did — the strongest possible hint,
  sitting twenty lines apart, unnoticed until the byte-level comparison.
- **Area:** `include/slate_uuids.hpp`
- **Impact:** **This is why "Slate service not found" survived N-16.** The
  service was in the GATT table all along after that fix — under the wrong
  UUID, so no central filtering on the real one could match it. It also
  explains why the *advertisement* carried a UUID the companion ignored.
- **Proof:** nRF Connect listed the 128-bit service as
  `f378f04c-6ee9-62a9-44fa-0000e979acfb`. Reversing the shipped `kService`
  array reproduces that string exactly.
- **Root cause:** NimBLE stores a 128-bit UUID as the text form reversed **in
  full** — byte 0 is the last byte of the string. The arrays reversed only
  *within* each group (`time_low`, `time_mid`, …), which is the Microsoft
  GUID mixed-endian convention, not NimBLE's. The comment even claimed
  "little-endian byte order for NimBLE", so the intent was right and the
  layout was wrong — invisible to review because the bytes *look* plausible.
- **Fix:** all four arrays rewritten as full reversals of their canonical
  strings, with the discriminant in bytes 8–9. New host test `test_uuids`
  formats each array back to text and pins it to the same string the
  companion hard-codes (`SlateUuids.kt`), and fails explicitly if the
  mixed-endian layout ever returns.
- **Note:** the DFU and stock services were unaffected (16-bit or supplied by
  NimBLE), which is exactly why everything *except* Slate's own service
  appeared to work.

### N-16 — GATT services registered too late; never in the attribute table

- **Status:** Fix staged (`0B8109A23AFA`, adds an on-device self-check);
  first attempt `F27CF0DD8D5A` still reported "service not found" from the
  companion, but see the Android GATT cache note below — the phone caches
  the service list per bonded device, and it had cached a database with no
  Slate service in it. **Forget the watch in Android Bluetooth settings and
  remove the CDM association before retrying**, or the phone will keep
  serving the stale table no matter what the firmware does.
- **Area:** `ble_nimble.cpp` `on_sync` / `start_stack`
- **Impact:** **The Slate service has never been discoverable.** With N-15
  clearing the way, the companion finally got far enough to say so:
  `ATT MTU: 247 (target 247)`, `MTU event: 247 status=0`, then
  `Error: Slate service not found`. The watch advertised the 128-bit service
  UUID while the GATT table did not contain it — so every session, every
  benchmark and every OTA attempt was doomed regardless of the link.
- **Root cause:** `ble_gatts_count_cfg` / `ble_gatts_add_svcs` for `g_svcs`
  (the Slate service **and** the Nordic DFU service beside it) ran inside the
  `on_sync` callback. `ble_hs_start()` calls `ble_gatts_start()` and *then*
  syncs, and per `ble_gatt.h`, queued services "get registered when
  `ble_gatts_start()` is called". Registering after that point queues them
  into a table that is never built again. The stock services (GAP, GATT, BAS,
  DIS) were unaffected because their `*_init()` calls already ran in
  `start_stack`, before the host — which is exactly why discovery appeared to
  work while our own service was missing.
- **Fix:** register `g_svcs` in `start_stack`, alongside the stock service
  inits and before `nimble_port_freertos_init`, so `ble_gatts_start()` builds
  the table from everything. `on_sync` now only sets identity and starts
  advertising. Failure codes 90/91 still report a registration problem.
- **On-device proof:** `on_sync` now calls `ble_gatts_find_svc()` on the
  Slate UUID after the table is built and reports **state 96** if it is
  absent. That separates "firmware still broken" from "phone cached the old
  table". Bring-up failure codes are now sticky, too — a registration
  failure in `start_stack` was previously overwritten by the later
  "advertising" mark, so the overlay could not have shown it.
- **Android GATT cache:** Android caches the discovered service database per
  bonded device and will keep returning the cached one after the peripheral
  changes. Because the watch genuinely had no Slate service until now, that
  stale cache is the expected state. Clearing it needs the device forgotten
  in Bluetooth settings (plus the CDM association removed), or a Bluetooth
  off/on. Worth surfacing in the companion under I-15 as an explicit
  remediation, since any firmware GATT change can trigger it.
- **Lesson for I-9:** this is the third defect in code that had never run.
  Init **ordering** against the stock NimBLE/InfiniTime sequence is its own
  audit axis — not just what we call, but when.

### N-15 — Mirror InfiniTime's connect path (stop driving negotiation)

- **Status:** Fix staged (`10F1FEF97FD8`); awaiting on-watch verification
- **Area:** `ble_nimble.cpp` GAP path, `local_core.cpp` link transitions
- **Impact:** With N-14 in place the tearing stopped, but the link still could
  not be held: MTU reached 247 for a moment, then the connection dropped, and
  association never completed. Overlay read `7.1873/820/3843/0.630` — worst
  phase **7** (link transition) at **1873 ms**, parse **0 ms**, render
  **630 ms**. So a single connect cost ~1.9 s of repainting (two to three
  full frames: `session.on_link_up` → `show_watch_face`, then
  `Core::on_link_up`), landing exactly while the central was discovering.
- **Root cause — a Slate invention at the mirror layer:** `run_negotiate()`
  ran **synchronously inside the `BLE_GAP_EVENT_CONNECT` callback**, issuing
  an ATT MTU exchange, a DLE request, a 2M PHY request and a connection
  parameter update, all at once, from the peripheral, while the central was
  mid-discovery. A mirrored peripheral does none of that: it publishes a
  preferred MTU and reacts to what the central does.
- **Fix:**
  - `ble_att_set_preferred_mtu(247)` once at sync; the central's own
    exchange then lands at 247 and arrives as `BLE_GAP_EVENT_MTU`, which is
    now recorded into STATUS. `BLE_GAP_EVENT_CONN_UPDATE` likewise records
    the interval the central chose.
  - The connect handler records the handle and signals session-up. Nothing
    else.
  - `run_negotiate()` is kept and exposed as `ble::negotiate_now()` for the
    roadmap A/B/D gates, which do need those parameters actively raised —
    but it must be called from the app task after discovery settles, never
    from a GAP callback.
  - Link transitions no longer repaint. `Core::on_link_up/on_link_down` and
    `show_watch_face` mark the face pending; `app_loop` coalesces them into
    one repaint outside the callback.
- **Note:** `parse = 0 ms` disproves the earlier "33 display-list parses"
  theory — the whole 630 ms is rasterise + SPI push. That is the next target
  (see N-13 follow-up and the parity row on `SpiMaster`).

### N-14 — Session hooks render on the NimBLE host task (tearing, dropped link)

- **Status:** Fix staged (`D98B9C174EB1`); awaiting on-watch verification
- **Area:** `main.cpp` session hooks, `ble_nimble.cpp` GAP event path
- **Impact:** On `DFD04D130924`: torn/corrupt face during reconnect, the
  companion showing `Connected: true` for a split second before dropping,
  association impossible. Looks like an N-13 regression; it is not — it is a
  pre-existing bug that the N-13 priority change exposed.
- **Root cause:** `notify_session_up()` / `notify_session_down()` are called
  from the GAP event path **on the NimBLE host task**
  (`ble_nimble.cpp:222` and the DISCONNECT case). The hooks ran
  `g_session.on_link_*()` and `g_core.on_link_*()` directly, and both end in
  `show_current()` → a full ~1.2 s parse+render. So every connect and
  disconnect did a 1.2 s blocking render on the host task, which (a) stalled
  all ATT traffic exactly when the central was negotiating, and (b) drove the
  interpreter and renderer concurrently with the app task's own repaint —
  hence the tearing. N-1 moved the *message* path off the host task but left
  these two hooks behind; with the app task previously at a higher priority
  the collisions were rarer, and equal priority made them constant.
- **Fix:** the hooks now only publish `{state, seq}` and wake the app task;
  `app_loop` applies the transition (loop phase 7) before draining messages,
  so session work, HELLO_OFFER and repaints all happen on the app task.
  Rapid flapping collapses to the latest state. Same rule as N-1.
- **Note:** the DIAG bench render callback has the same shape and also runs
  on the host task. It is inert in release (`SLATE_BLE_DIAG=0`) but should be
  moved before diag builds are used again.

### N-13 — App loop iterates ~30x slower than designed → BLE unusable

- **Status:** **Diagnosed on hardware; fix staged (`DFD04D130924`)**
- **Diagnosis:** overlay read `3.1236` — worst phase **3** (session/core
  tick) at **1236 ms**, not phase 6. So the app task was never starved; it
  was doing over a second of blocking work. Two causes:
  1. `Interpreter::render_retained_to_display` re-parses the **entire**
     display list once per tile — 30 tiles, plus the validate, side-effect
     and meta passes: 33 full parses and 30 rasterise+SPI passes per
     repaint. `Core::tick` triggered that unconditionally every 2 s, and
     the diag overlay triggered another every 16 iterations.
  2. The app task ran at `tskIDLE_PRIORITY + 2`, **above** the NimBLE host
     task at +1. That render therefore blocked all ATT traffic, which is
     why MTU stayed at 23, service discovery never completed, the link
     dropped on supervision timeout, and every benchmark failed to send
     (`THRU_START send failed — is GATT ready?`).
- **Fix:** app task moved to `tskIDLE_PRIORITY + 1`, equal to the NimBLE
  host, so time slicing interleaves them (and mirroring InfiniTime, where
  MAIN/DisplayApp sit at or below the BLE host); `Core::tick` repaints only
  when the step count or the displayed minute actually changed; the diag
  overlay repaint is time-based (2 s) instead of per-16-iterations. Worst
  parse+render is now surfaced as the overlay's 4th second-line field.
- **Follow-up (not done):** the per-tile re-parse is the underlying cost and
  wants dirty-rect rendering so an unchanged region is not re-rasterised 30
  times. Do that under I-10 rather than blind — it needs the renderer's
  dirty tracking to be trustworthy first.

### N-11 — FreeRTOS tick IRQ ran at priority 0 (double-shifted priority)

- **Status:** **Resolved — verified on hardware** (reset reason back to 4, no
  watchdog resets, recovered-ticks 0, no cyan asserts)
- **Area:** `port/nrf52/src/port_rtc_tick.c` `vPortSetupTimerInterrupt`
- **Impact:** Latent since the RTC1 tick port was written; the most likely
  true cause of the N-10 reboot loop (and a plausible contributor to older
  "unexplained" instability). Found when the N-10 build painted **cyan
  `397105A1` / `0000037A`** = `port.c:890`,
  `configASSERT(ucCurrentPriority >= ucMaxSysCallPriority)` — the N-10
  catch-up added `xTaskGetTickCountFromISR()`, the first self-validating
  FromISR call in that handler, which immediately reported the bad priority.
- **Root cause:** `NVIC_SetPriority(portNRF_RTC_IRQn,
  configKERNEL_INTERRUPT_PRIORITY)`. CMSIS shifts internally
  (`p << (8 − __NVIC_PRIO_BITS)`), but `configKERNEL_INTERRUPT_PRIORITY` is
  the **already-shifted** register form FreeRTOS writes straight into SHPR
  (`7 << 5` = 224). Shifted twice: `(224 << 5) & 0xFF` = **0**. So the tick
  ran at priority 0 — above NimBLE's RADIO (5) and above
  `configMAX_SYSCALL_INTERRUPT_PRIORITY` (3). `portENTER_CRITICAL` raises
  BASEPRI to 3, which cannot mask priority 0, so `xTaskIncrementTick()`
  could mutate the delayed-task lists **inside another task's critical
  section** — matching the observed late/never-scheduled app task
  (~460 ms iterations) better than tick coalescing did.
- **Fix:** pass the raw number (`configLIBRARY_LOWEST_INTERRUPT_PRIORITY`,
  7) plus a `_Static_assert` that the argument is a raw priority, so this
  cannot regress. Tick now sits at priority 7 as intended — which is also
  what makes N-10's catch-up genuinely necessary (below RADIO at 5, TICK
  events really can coalesce). Both fixes ship together.
- **Audited:** the only other `NVIC_SetPriority` in Slate code is via
  `nrfx_glue` (unused); GPIOTE (`cst816s.cpp`) runs at default priority 0
  but only sets a flag and clears events — no FreeRTOS API calls, so it is
  safe, though worth revisiting under I-9 since it can pre-empt the radio.

### N-10 — FreeRTOS tick loses time under BLE load → watchdog reboot loop

- **Status:** **Resolved — verified on hardware** (recovered-ticks reads 0, so
  with N-11 fixed the tick no longer falls behind; the catch-up remains as
  insurance and for the tickless path, I-3)
- **Area:** `port/nrf52/src/port_rtc_tick.c` (RTC1 tick), mirror-rule gap
- **Impact:** With the radio finally live (N-9), the watch reboots every
  ~30 s. Diag read `6/22/3/0/940/7.0`: reset reason **6 = soft|watchdog**
  (bit 2 set for the first time), advertising healthy, but only **3 paints
  in 22 s of real time** — the app loop's `pdMS_TO_TICKS(20)` wait was
  taking ~460 ms, i.e. the FreeRTOS tick running ~20x slow. nRF Connect
  sees the watch, connects, then `GATT CONN TIMEOUT (0x8)` ~37 s later —
  that is the watch rebooting under it, not a GATT fault.
- **Root cause:** the RTC1 handler runs at kernel (lowest) interrupt
  priority, below NimBLE's RADIO ISR (priority 5) and level with its
  RTC0/TIMER0 ISRs, so once the radio is live it is regularly held off past
  the 0.98 ms tick period. RTC TICK events **coalesce** (the peripheral just
  re-sets one flag), so "one increment per TICK event" silently drops time
  and the FreeRTOS clock free-runs slow. Everything scheduled in ticks
  stretches in real time — including `app_loop`'s 20 ms wait and therefore
  the WDT pet cadence — until the bootloader's real-time 7 s dog bites.
  `mono_ms()` reads the RTC1 COUNTER directly, which is why uptime looked
  sane while tick-scheduled work crawled.
- **Fix:** restore InfiniTime's catch-up (`diff = (COUNTER −
  xTaskGetTickCount()) & MAXTICKS`, stock `port_cmsis_systick.c`
  behaviour — the mirror-rule default) with the hard guards the file header
  demanded: diff in the top half of the range = tick is ahead (step once,
  never ~16M — the original underflow-spin regression), diff 0 = step once,
  and ≤128 ticks stepped per ISR so the handler stays short. Recovered
  ticks are counted and shown as the diag overlay's **7th field**.
- **Verify:** overlay is now
  `reset/uptime/paints/button/stall/ble.rc/recovered_ticks`. Expect no
  watchdog resets (reset reason back to 4), paints ≈ uptime ÷ 0.32, and a
  climbing recovered-ticks value quantifying what used to be lost. Tickless
  (I-3) inherits this fix — note it in the I-9 parity table as aligned.

### N-9 — Slate up but radio silent: advertising never starts on hardware

- **Status:** **Resolved — verified on hardware.** Diag read `7.0`,
  nRF Connect sees "SLATE" at `E8:01:34:22:08:89` (static-random, top bits
  `11` as required) and connects. Reboot loop that followed is N-10.
- **Area:** `ble_nimble` / NimBLE port bring-up
- **Impact:** No BLE at all on `D7E86854B9D3`/`2284BA304B8F`: nothing in
  nRF Connect, CDM finds nothing, IMAGE_OK cannot confirm. UI healthy;
  button-hold revert **verified working** on hardware (first field proof of
  the R-3/N-2 WDT fix).
- **Root cause (from telemetry image `2284BA304B8F`, diag read `93.21`):**
  `ble_gap_adv_start` → `BLE_HS_ENOADDR` (21). Controller synced and GATT
  registered fine, but no identity address was ever configured — nRF52 has
  no public IEEE address, and the standalone port never set the
  FICR-derived static-random identity (same class of gap as the missing
  `ble_ll_init` sysinit call).
- **Fix:** `on_sync` forms the static-random address from `FICR.DEVICEADDR`
  (top two bits set per spec; stable across boots so CDM associations and
  bonds survive), `ble_hs_id_set_rnd` + `ble_hs_id_infer_auto`, and both
  adv starts use the inferred own-addr type. Failure code 95 added for the
  identity path. Telemetry stays in tree: diag overlay 6th field `state.rc`
  (`ble::bringup_snapshot`, `include/ble_gatt.hpp`): 7.0 = advertising,
  1–6 = bring-up stage reached, 90–95 = named failure with rc.

### I-3 — FreeRTOS tickless idle on RTC1 not soak-tested

- **Status:** Open (disabled by default)
- **Area:** `port/nrf52/src/port_rtc_tick.c`, `config/FreeRTOSConfig.h`
- **Impact:** Higher idle current until safe to enable; wrong enable bricks sealed units
- **Notes:** Enabling tickless previously led to paints ≈4 then InfiniTime at ~7 s
  (ISR catch-up underflow after `vTaskStepTick`). Suppress path was adjusted;
  `configUSE_TICKLESS_IDLE` remains `0` until sealed soak proves hold-reset and
  BLE stay healthy. See `docs/freertos-tickless.md`.

---

## Firmware UX / correctness

### I-4 — Local UI glyphs unreadable (3×5 TEXT)

- **Status:** Resolved (`text_big` / TEXT_SCALED on all local screens)
- **Area:** `src/local_ui.cpp`
- **Impact:** Was: watchface / notifs / settings / alert unreadable at arm’s length
- **Notes:** Face already used scale 8/3; notifs/settings/alert/disconnected now
  use `text_big` (≥3 body, 8 for hero). `text_digits` removed. Host:
  `test_local_ui` (6-row notifs ≈296 B ≤ 512 `dl_buf_`).

### I-5 — Battery percent shows 0 while charging

- **Status:** Mitigated (InfiniTime curve + unknown + charge-edge resample)
- **Area:** `battery` / `battery_hw` / `local_core` / face
- **Impact:** Was: charge bolt + numeric 0 until first/60s SAADC
- **Notes:** `kPercentUnknown` (0xFF) → face `--`; charge-pin edge + 10 s ADC via
  `Core::sample_battery` hook; InfiniTime 6-point mV curve + 99% charge clamp +
  hysteresis. OTA/DFU treat unknown as blocked. Host: `test_battery`. Validate
  `adc*2000/1241` vs multimeter before trusting curve breakpoints absolutely.

### N-1 — SDP parse/render on NimBLE host task (link→app handoff)

- **Status:** Resolved (I-10 stage 1)
- **Area:** `ble_link` / `ble_app_inbox` / `main` app task
- **Impact:** Was: GATT write callback ran session + interpreter + ST7789 SPI on
  the BLE host task, racing app-task local UI (torn renders; ~115 ms host stalls)
- **Notes:** Host only reassembles + `AppInbox::try_push` (zero-copy since
  N-8: inbox borrows the reassembler buffer, ingest gated while pending;
  busy → drop-and-count). App task `drain_app_messages` then
  `on_app_message`; CREDIT after deferred `on_display` apply. Wake via
  `xTaskNotifyGive`. Host: `test_ble_app_inbox`. DIAG loopback on producer
  except during the pending window.

### N-4 — Frame reassembler: per-channel state but one shared 4 KiB buffer

- **Status:** Resolved (single-in-flight rule, zero RAM)
- **Area:** `sdp_frame` reassembler + companion frame writer
- **Impact:** Was: interleaved multi-fragment messages on different channels
  silently corrupted each other in the shared `buf_`
- **Notes:** §4.2 now specifies single-in-flight: a FIRST on any channel
  (incl. FIRST|LAST) aborts an in-flight reassembly on another channel
  (reject-and-resync; `Reassembler::preempt_drop_count()`, folded into
  `Link::drop_count()`). Channel-rejected frames (6/7-release) don't abort.
  Companion: `SdpWriteQueue` (sdp-core) owns per-channel TX seq and enqueues
  each message's fragments atomically (replaces `ChannelSeq` + raw queue in
  `SlateGattClient`); `SdpReassembler.kt` mirrors the abort rule. Host:
  `test_sdp_frame` interleave cases; Kotlin: `SdpWriteQueueTest` (incl.
  concurrent enqueue), `SdpFrameTest` preempt cases. Wire format unchanged.

### N-6 — HELLO_OFFER encoder had unguarded buffer writes

- **Status:** Resolved (capacity-checked writes + build-time size guard)
- **Area:** `session.cpp` HELLO_OFFER encoder
- **Impact:** Was: `put_u16(tmp + n, …)` / `fill_opcode_bitmap(tmp + n)` wrote
  unbounded, and `push` silently truncated at capacity — catalog growth would
  have become a stack overflow or a corrupt offer
- **Notes:** All writes now go through `push`/`push_u16`/`push_bytes` with an
  `ok` flag; any overflow returns 0 (like the local-UI writer `W`).
  `kHelloOfferMaxBytes` (worst case from `profile::kCatalogCount` ×
  (4 + name ≤ `kMaxProfileNameLen`) + fixed fields) is `static_assert`ed
  against `kHelloOfferBufBytes` (160) in `session.hpp`, so catalog growth
  fails the build. Host: `test_session` byte-exact golden (84 B) + overflow
  cases; Kotlin: `HelloOfferGoldenTest` pins the same bytes on
  `SessionTestFixtures.encodeHelloOffer` and drives them through
  `SessionClient.parseHelloOffer` (decoder coverage).

### I-6 — IMAGE_OK confirm vs single BLE connection

- **Status:** Mitigated (dwell mechanism kept as documented InfiniTime exception)
- **Area:** `boot_util` / companion / field process
- **Impact:** Easy to miss confirm; next reset reverts unconfirmed image
- **Notes:** Amber bar = unconfirmed. Any central for ~10 s writes `IMAGE_OK`.
  Companion queries CONTROL `CONFIRM_STATUS` (0xE1) for in-app countdown;
  `LinkContention` warns when another app holds GATT. See
  `docs/flash-sealed.md` step 8 and `docs/infinitime-parity.md`.

### I-7 — Nordic DFU “failed” on successful reboot/swap

- **Status:** Open
- **Area:** Companion sealed DFU client
- **Impact:** False failure after a transfer that actually swapped
- **Notes:** Watch resets into MCUBoot; phone often reports failure. Operators
  should check the watch face / InfiniTime, not only the app toast.

### I-8 — Sealed DFU target classification is heuristic

- **Status:** Open (accepted residual risk)
- **Area:** Companion preflight
- **Impact:** Wrong zip/target on a non-InfiniTime BL map could brick
- **Notes:** MCUBoot has no BLE; InfiniTime vs PineDFU/SoftDevice heuristics can
  reject bad zips but cannot prove BL layout. See `docs/flash-sealed.md`.

---

## Architecture / deferred

### I-9 — Audit remaining InfiniTime low-level divergences

- **Status:** Open (partial table started)
- **Area:** Boot, DFU, BLE advertising, power, RTC ownership
- **Impact:** Process debt; avoid inventing sealed-watch “improvements”
- **Notes:** Living table: [`docs/infinitime-parity.md`](docs/infinitime-parity.md).
  **WDT pet site** marked **aligned** (app-task-only reload; tick/idle do not pet).
  Continue audit (adv name/intervals, DFU service, sleep, confirm UX exception, etc.).

### I-10 — FreeRTOS task topology split deferred

- **Status:** Deferred (stage 1 done — link→app handoff)
- **Area:** Scheduler / roadmap §3.1
- **Impact:** Single app task + NimBLE ll/host only
- **Notes:** N-1 shipped: GATT → `AppInbox` → app-task drain (no host-task
  render). Remaining `display` / `link` / `sensors` / `system` split not started.

### I-11 — HRS GATT / HRS3300 driver deferred

- **Status:** Deferred
- **Area:** BLE / sensors
- **Impact:** No heart-rate service
- **Notes:** `ble_gatt` flag off; HR chip still needs sleep-at-boot care.

### I-12 — GATT CTS deferred

- **Status:** Deferred
- **Area:** Time sync
- **Impact:** No standard CTS client
- **Notes:** Time uses CONTROL `0x20` (`TimeSync.kt`); not GATT CTS.

### I-13 — Channel-5 SDP OTA not proven on durable Slate

- **Status:** Open
- **Area:** Firmware `ota_xfer` + companion OTA UI
- **Impact:** Preferred post-install update path unverified end-to-end on hardware
- **Notes:** Implementation exists; blocked on stable boot + `IMAGE_OK` + companion
  link. Prefer after I-1/I-2 cleared.

### I-14 — RAM headroom tight (~93% in prod link map)

- **Status:** Watch
- **Area:** Linker / CI RAM gate
- **Impact:** NimBLE / heap regressions can trip cyan asserts or CI
- **Notes:** Hard gate: static + heap ≤ 54 KiB with ≥ 6 KiB slack. Heap bumped
  historically for NimBLE eventq; keep monitoring. N-1's `AppInbox` is
  zero-copy since N-8 (~12 B metadata, not 4 KiB); current link:
  RAM 60944 B / 64 KB, 496 B below the MSP stack reserve — the margin is
  thin, so any static growth needs a map check.

---

## Companion / ops

### I-15 — CDM / reconnect useless when watch is not advertising

- **Status:** Open (expected limitation; UX unclear)
- **Area:** Companion CDM + link service
- **Impact:** User confusion during soft-brick / InfiniTime-only recovery
- **Notes:** Associate / Start reconnect cannot find a silent radio. Sealed
  installer path must steer users to InfiniTime/recovery CDM + DFU.

### I-16 — Multi-app BLE contention (Gadgetbridge et al.)

- **Status:** Mitigated (`LinkContention` + troubleshooting screen)
- **Area:** Companion + `ble_nimble` adv resume
- **Impact:** Blocks HELLO, confirm, OTA, DFU when another central holds the slot
- **Notes:** `SLATE_BLE_MAX_CONNECTIONS 1`. Instant GATT occupancy + bonded/
  connect-fail hints; app-specific remediation copy in `LinkContention`.
  Firmware resumes advertising on failed connect (`ble_gap_adv_policy`).
  See companion Troubleshooting and `docs/flash-sealed.md` step 7.

---

## Resolved recently (keep for context; remove when stale)

| ID | Summary | Resolution |
|---|---|---|
| R-1 | SysTick tickless never wakes on nRF52 System ON sleep | RTC1 tick port; SysTick tickless `#error` |
| R-2 | Charging forced `Screen::Charging` tiny green glyphs | Stay on face; charge icon beside battery |
| R-3 | WDT always petted from tick (wedged app never resets) | Button withhold in `wdt::pet()`; tick/idle **no longer pet** — only `app_loop` (+ flash helpers / future POST_SLEEP). Host: `wdt_hold`, `wdt_app_pet`. See `docs/infinitime-parity.md`. |
| R-4 | Button strobe too short → hold never seen | Enable left high (InfiniTime); simple sense read |
| R-5 | SPI `EVENTS_END` infinite spin (freeze with N-2) | Bounded wait + SPIM recover + bool errors; dirty-tile retry; always-on CS checks (`spi_bus`) |

---

## Suggested next order of work

1. Finish **I-1** recovery (discharge → pinecone red/blue → InfiniTime DFU).
2. Package and flash **I-2** (WDT withhold + safe RTC tick, tickless off).
3. Confirm BLE + amber → `IMAGE_OK` (**I-6** / **I-16**).
4. Fix **I-4** (readable local UI) before further sealed experiments.
5. Revisit **I-3** tickless only after soak; then **I-13** SDP OTA.
