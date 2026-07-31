# Open issues — Slate / EvoTime

Last updated: 2026-07-31

Standing rule: **mirror InfiniTime** for boot, MCUBoot/DFU, flash map, WDT/button
reset, and BLE bring-up
([InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime)). Differ on purpose only
at the SDP / companion JS display-list layer.

---

## Active / blocking

### I-1 — Sealed watch soft-brick (no BLE, no InfiniTime)

- **Status:** Open (field recovery); **runbook + regression fence done**
- **Area:** Firmware / sealed DFU
- **Impact:** Watch unusable over the air until recovered
- **Notes:** After RTC-tick DFU (`slate-dfu.zip` SHA prefix `790822E97A78`; earlier
  tickless package `CC3F25A12AB2` also regressed), device shows Slate’s tiny
  glyphs, does not advertise, and InfiniTime does not return. Procedure:
  [`docs/recovery-sealed.md`](docs/recovery-sealed.md). In-tree WDT withhold
  mirrors InfiniTime; host test `wdt_hold` fences RR0 reload while held. User
  still needs discharge → pinecone → InfiniTime → fixed Slate flash (I-2).

### I-2 — Fixed Slate DFU package (post-recovery flash)

- **Status:** Package ready; **awaiting on-device flash** (after I-1 recovery)
- **Area:** Firmware packaging
- **Impact:** Blocks return to Slate bring-up until flashed
- **Artifact:** `build/dfu/slate-dfu.zip`
  - **SHA-256:** `C3AE3F15E40888309733311C2697B14BFEC3CEF67BBCF5465967573D1415A8C3`
  - **SHA-256 prefix (12):** `C3AE3F15E408`
  - Built 2026-07-31 (night): `BOOTLOADER_PRESENT=ON`, `SLATE_HAS_NIMBLE=ON`,
    `imgtool create --slot-size 475136` (InfiniTime contract, unsigned)
  - Includes N-1 link→app handoff (zero-copy per N-8), N-4 single-in-flight
    framing, N-6 HELLO_OFFER hardening, N-9 BLE identity fix (verified on
    hardware), N-10 RTC1 tick catch-up, N-11 tick IRQ priority fix
  - Link map: FLASH ~121608 B / 475104 B; RAM ~60944 B / 64 KB
    (`__heap_end__` 496 B below `__StackLimit`)
  - Supersedes `E95E6CD9676B` (catch-up alone → cyan assert `port.c:890`,
    which exposed N-11), `4324FC495E0C` (N-9 verified, then N-10 reboot),
    `2284BA304B8F` (telemetry build that diagnosed N-9 via `93.21`),
    `D7E86854B9D3` (radio silent, no diag), `8B2D9054E9E7` (pre-N-1 tree)
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

### N-11 — FreeRTOS tick IRQ ran at priority 0 (double-shifted priority)

- **Status:** Fix staged (`C3AE3F15E408`); awaiting on-watch verification
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

- **Status:** Fix staged (`C3AE3F15E408`, with N-11); awaiting verification
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
