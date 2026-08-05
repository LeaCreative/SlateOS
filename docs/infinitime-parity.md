# InfiniTime low-level parity

Standing policy (`CLAUDE.md`): mirror InfiniTime for boot, MCUBoot/DFU, flash,
WDT/button, and BLE bring-up; differ on purpose only at the SDP / JS layer.

| Area | InfiniTime | Slate | Verdict |
|---|---|---|---|
| **WDT pet site** | `SystemTask::Work()` ~100 ms loop calls `watchdog.Reload()` only when button released (`nrf_gpio_pin_read(Button)==0`) | `app_loop` calls `wdt::pet_service()` each iteration (~20 ms / ~200 ms ambient). `vApplicationTickHook` / `vApplicationIdleHook` do **not** pet. `configPOST_SLEEP_PROCESSING` → `slate_wdt_pet` reserved for tickless (I-3, sleeps capped 1 s). Button withhold inside `wdt::pet()`. Long secondary erase pets in `ota_slot::erase_all` / `xt25::wait_ready`. | **aligned** (same outcome: wedged main task → bootloader reset ≤ ~7 s; different call site names) |
| **Button enable** | Left high for life of app | `board::button_hw_init` OUTSET enable, leave high | **aligned** |
| **Confirm / validate** | Settings → Firmware → Validate writes IMAGE_OK | Any BLE central held `kConfirmDwellMs` (10 s); companion surfaces trial countdown via CONTROL `CONFIRM_STATUS` (0xE1) + GATT contention check (`LinkContention`) | **documented exception** — thin client has no local Validate UI; dwell proves radio before point of no return (`docs/flash-sealed.md`). Visibility/conflict UX on phone; mechanism unchanged (I-6). |
| **Adv after failed connect** | `OnGAPEvent` restarts adv when `connect.status != 0` | Same via `ble_gap_adv_policy` + `resume_advertising()` in `ble_nimble.cpp` | **aligned** (I-16) |
| **Battery %** | 6-point mV curve, charge clamp 99%, up-only while charging / down-only while discharging; measure on charge GPIOTE + timer | Same curve/clamp/hysteresis in `battery.cpp`; sample on charge edge + 10 s (`Core::poll_battery`); unknown=`0xFF`/`--` | **aligned** (ADC gain path differs — validate mV with meter) |
| **GATT → UI work** | Callbacks `PushMessage` to SystemTask / DisplayApp; no render in BLE host | Host: reassemble + `AppInbox` borrow; app task `drain_app_messages` → session/interp/SPI; CREDIT after deferred apply. **Session up/down hooks also deferred to the app task (N-14)** — they rendered on the host task until 1 Aug | **aligned** — but note this row read "aligned" for a week while N-14 sat in it. Verify by tracing every path into the renderer, not by reading the message path. |
| **Connect handling** | Peripheral records the connection and reacts; preferred MTU published in config; central drives MTU exchange, DLE, PHY and connection parameters | Was: `run_negotiate()` issued MTU + DLE + 2M PHY + param update synchronously inside `BLE_GAP_EVENT_CONNECT`, which collapsed the link right after MTU reached 247. Now: preferred MTU set at sync; connect only records the handle and signals session-up; `BLE_GAP_EVENT_MTU` / `CONN_UPDATE` record what the central chose. `ble::negotiate_now()` retained for the A/B/D gates, app-task only | **aligned** (N-15) |
| **Link state → display** | BLE layer posts a message; DisplayApp redraws on its own schedule | Link transitions mark the face pending; `app_loop` coalesces into one repaint outside the GAP callback | **aligned** (N-15) |
| **Flash / MCUBoot map** | Primary 475136, secondary SPI `@0x40000` | Same InfiniTime contract (`flash_map.hpp`, `imgtool --slot-size 475136`) | **aligned** |
| **RTC ownership** | RTC0 NimBLE; RTC1 FreeRTOS tick | Same (`SLATE_FREERTOS_RTC_TICK`) | **aligned** (tickless still off — I-3) |
| **SPI master** | IRQ-driven `SpiMaster`, shared display+flash CS | Busy-wait SPIM0 with **bounded** `EVENTS_END` wait (5 ms/chunk), recover + bool errors, always-on CS discipline checks (`src/spi_bus.cpp`) | **proportionate step** — escalate to full InfiniTime SpiMaster if behaviour keeps diverging |

| **GATT service registration** | All services registered during controller init, then GATT started explicitly, before anything uses the table | Was: `ble_gatts_count_cfg` / `add_svcs` for `g_svcs` ran in the `on_sync` callback — after `ble_hs_start()` had already called `ble_gatts_start()` — so the Slate and DFU services were advertised but absent from the attribute table. Now registered in `start_stack` before `nimble_port_freertos_init` | **aligned** (N-16) |

Expand this table as further I-9 audits land.

**Audit note.** Three of the defects found on hardware (N-9 identity, N-15
connect handling, N-16 registration) were about **init ordering and who
drives**, not about which calls exist. Reading the call list finds neither.
When auditing a row here, check the sequence against the stock NimBLE
peripheral flow and ask which side initiates — that is where this port kept
diverging.

---

# I-9 divergence audit — started 5 Aug 2026

**Reference tree:** `C:\Users\highj\Documents\Projects\InfiniTime-main`

## Why this exists

Four separate defects this session were places Slate deviated from InfiniTime
on low-level hardware. Each was found by hitting it on the watch and then
debugging backwards, which is the most expensive possible order. The operator
has stated repeatedly that low-level behaviour may simply mirror InfiniTime —
so divergence should be the exception, justified in writing, not the default.

## Divergences found so far

| Area | InfiniTime | Slate (before fix) | Outcome |
|---|---|---|---|
| **Touch read shape** | 6 bytes from register **1** | 63 bytes from register **0** | Every read failed. `readfail` 12/12 |
| **Touch controller config** | Wake reads `0x15`/`0xA7`, then writes `0xEC` motion mask, **`0xFA` IRQ control**, `0xFB` auto-reset off | Hard reset + one probe byte; **no control registers written at all** | IRQ behaviour undefined; pin sat asserted |
| **Touch IRQ trigger** | Edge-triggered GPIOTE | `SENSE_LOW` re-armed unconditionally | 7980 interrupts in 69 s (~115/s) |
| **Battery ADC** (earlier, N-12) | SAADC 10-bit, gain 1/4, 0.6 V ref | Mismatched config and formula | Battery read 0% |

## Confirmed *aligned* — no action

- **TWI bus recovery.** InfiniTime `TwiMaster::FixHwFreezed()` toggles ENABLE
  to unstick a frozen peripheral; Slate has `twi.cpp::recover_bus()` called
  from all three error paths. Equivalent.
- **WDT / button-hold reset.** Documented above; deliberate and matching.

## Still to audit — file pairs

| Subsystem | InfiniTime | Slate |
|---|---|---|
| Display init/sleep | `drivers/St7789.cpp` | `src/st7789.cpp` |
| SPI master | `drivers/SpiMaster.cpp` | `src/spi_bus.cpp` |
| External flash | `drivers/SpiNorFlash.cpp` | `src/xt25.cpp` |
| Accelerometer | `drivers/Bma421.cpp` | `src/bma.cpp` |
| Heart rate | `drivers/Hrs3300.cpp` | (deferred, I-11) |
| Pin map | `drivers/PinMap.h` | `CLAUDE.md` hardware block |
| Touch **sleep/wake** | `Cst816S::Sleep()` / `Wakeup()` | **absent in Slate** — see below |

## Known gap, not yet fixed

`Cst816S::Sleep()` / `Wakeup()` exist in InfiniTime and have **no Slate
equivalent**. Slate instead disables the whole TWI bus via
`power::buses_idle()`, which is why `poll()` now needs a full `twi::init()`
before every read. Mirroring their per-device sleep would be cheaper and is
the likely correct fix — relevant to I-17 (display sleep) and N-31.

## Audit axes (from the N-series post-mortem)

These four are where the defects actually were, so check them first:

1. **Initialisation ordering** — what is configured, and in what order.
2. **Who initiates** — watch or phone, driver or caller.
3. **Worst-case duration inside a callback or ISR.**
4. **Who writes peripheral ENABLE registers** — and what else that costs.

---

## Scoped mirroring task (owner request, 5 Aug): battery, sleep/wake, input, vibration

### 1. Vibration — **divergence found, not yet fixed**

| | InfiniTime | Slate |
|---|---|---|
| Driver | `components/motor/MotorController.cpp` | **none** — `board::pulse_motor()` only |
| Pin | `PinMap::Motor = 16`, active low (`pin_set` = off) | verify Slate matches |
| Timing | **FreeRTOS timers** (`xTimerCreate`), non-blocking | **`board::busy_wait_ms()` on the app task** |

`main.cpp::do_haptic()` busy-waits up to 80 ms, and the DOUBLE pattern
busy-waits 25 + 40 + 25 = **90 ms** inline. That runs on the app task, which
also drains the SDP inbox — so every haptic directly worsens the stall that
causes inbox drops (P-8/N-35). InfiniTime never blocks: it starts a one-shot
timer and returns.

**Fix:** port `MotorController`'s timer approach. A `MotorController` with
`RunForDuration()` backed by a FreeRTOS one-shot timer, replacing the
busy-wait entirely.

### 2. Sleep / wake — **known gap**

`Cst816S::Sleep()` / `Wakeup()` have no Slate equivalent; Slate disables the
whole TWI bus instead (`power::buses_idle()`), forcing a full `twi::init()`
per touch read. Mirror per-device sleep. Interacts with I-17 (display never
sleeps) and N-31.

Compare `systemtask/SystemTask.cpp` sleep/wake handling and
`displayapp/DisplayApp.cpp` idle handling against `src/power.cpp`.

### 3. Battery

N-12 already aligned the SAADC config and formula. Remaining check:
`components/battery/BatteryController.cpp` vs `src/battery.cpp` +
`src/battery_hw.cpp` — specifically **when** sampling is triggered, charge-pin
debounce, and whether InfiniTime filters/averages readings.

### 4. Input

Touch driver now mirrors InfiniTime (read shape, config registers, edge IRQ).
Remaining: compare gesture decoding and the button path in
`systemtask/SystemTask.cpp` against `src/input_event.cpp` /
`src/input_router.cpp` — especially debounce and how a gesture maps to an
action.
