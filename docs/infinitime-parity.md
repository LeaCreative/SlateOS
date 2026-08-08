# InfiniTime low-level parity

Standing policy (`CLAUDE.md`): mirror InfiniTime for boot, MCUBoot/DFU, flash,
WDT/button, and BLE bring-up; differ on purpose only at the SDP / JS layer.

| Area | InfiniTime | Slate | Verdict |
|---|---|---|---|
| **WDT pet site** | `SystemTask::Work()` ~100 ms loop calls `watchdog.Reload()` only when button released (`nrf_gpio_pin_read(Button)==0`) | `app_loop` calls `wdt::pet_service()` each iteration (~20 ms / ~200 ms ambient). `vApplicationTickHook` / `vApplicationIdleHook` do **not** pet. `configPOST_SLEEP_PROCESSING` → `slate_wdt_pet` reserved for tickless (I-3, sleeps capped 1 s). Button withhold inside `wdt::pet()`. Long secondary erase pets in `ota_slot::erase_all` / `xt25::wait_ready`. | **aligned** (same outcome: wedged main task → bootloader reset ≤ ~7 s; different call site names) |
| **Button enable** | Left high for life of app | `board::button_hw_init` OUTSET enable, leave high | **aligned** |
| **Confirm / validate** | Settings → Firmware → Validate writes IMAGE_OK | Any BLE central held `kConfirmDwellMs` (10 s); companion surfaces trial countdown via CONTROL `CONFIRM_STATUS` (0xE1) + GATT contention check (`LinkContention`) | **documented exception** — thin client has no local Validate UI; dwell proves radio before point of no return (`docs/flash-sealed.md`). Visibility/conflict UX on phone; mechanism unchanged (I-6). |
| **Adv after failed connect** | `OnGAPEvent` restarts adv when `connect.status != 0` | Same via `ble_gap_adv_policy` + `resume_advertising()` in `ble_nimble.cpp` | **aligned** (I-16) |
| **Battery %** | 6-point mV curve, charge clamp 99%, up-only while charging / down-only while discharging; measure on charge GPIOTE + timer; **no voltage filtering** | Same curve/clamp/hysteresis in `battery.cpp`; sample on charge edge + 10 s (`Core::poll_battery`); unknown=`0xFF`/`--` | **aligned**, re-verified line by line 8 Aug — same six curve points, same ratchet condition, same absence of a filter. Do not "fix" the visible consequence without reading **I-18** in `docs/issue-prompts-open.md` first |
| **GATT → UI work** | Callbacks `PushMessage` to SystemTask / DisplayApp; no render in BLE host | Host: reassemble + `AppInbox` borrow; app task `drain_app_messages` → session/interp/SPI; CREDIT after deferred apply. **Session up/down hooks also deferred to the app task (N-14)** — they rendered on the host task until 1 Aug | **aligned** — but note this row read "aligned" for a week while N-14 sat in it. Verify by tracing every path into the renderer, not by reading the message path. |
| **Connect handling** | Peripheral records the connection and reacts; preferred MTU published in config; central drives MTU exchange, DLE, PHY and connection parameters | Was: `run_negotiate()` issued MTU + DLE + 2M PHY + param update synchronously inside `BLE_GAP_EVENT_CONNECT`, which collapsed the link right after MTU reached 247. Now: preferred MTU set at sync; connect only records the handle and signals session-up; `BLE_GAP_EVENT_MTU` / `CONN_UPDATE` record what the central chose. `ble::negotiate_now()` retained for the A/B/D gates, app-task only | **aligned** (N-15) |
| **Link state → display** | BLE layer posts a message; DisplayApp redraws on its own schedule | Link transitions mark the face pending; `app_loop` coalesces into one repaint outside the GAP callback | **aligned** (N-15) |
| **Flash / MCUBoot map** | Primary 475136, secondary SPI `@0x40000` | Same InfiniTime contract (`flash_map.hpp`, `imgtool --slot-size 475136`) | **aligned** |
| **RTC ownership** | RTC0 NimBLE; RTC1 FreeRTOS tick | Same (`SLATE_FREERTOS_RTC_TICK`) | **aligned** (tickless still off — I-3) |
| **SPI master** | IRQ-driven `SpiMaster`, shared display+flash CS | Busy-wait SPIM0 with **bounded** `EVENTS_END` wait (5 ms/chunk), recover + bool errors, always-on CS discipline checks (`src/spi_bus.cpp`) | **proportionate step** — escalate to full InfiniTime SpiMaster if behaviour keeps diverging |
| **TWI transfer mechanism** | **No SHORTS at all.** `TwiMaster` spins on `EVENTS_TXSTARTED`/`LASTTX` (DWT cycle-count timeout → `FixHwFreezed`), then issues `TASKS_STOP` or `TASKS_SUSPEND` by hand. A register read is `Write(addr, &reg, 1, stop=false)` → SUSPEND, then `Read(...)` → `TASKS_RESUME` + `STARTRX` → STOP. Their source carries the comment *"TODO use shortcut to automatically send STOP when receive LastTX"* | SHORTS-driven: `LASTTX_STOP` for a write, `LASTTX_STARTRX \| LASTRX_STOP` for a register read, then one bounded wait on `EVENTS_STOPPED` (`src/twi.cpp`) | **deliberate divergence — decided 5 Aug 2026, after the fact.** See below. |

| **GATT service registration** | All services registered during controller init, then GATT started explicitly, before anything uses the table | Was: `ble_gatts_count_cfg` / `add_svcs` for `g_svcs` ran in the `on_sync` callback — after `ble_hs_start()` had already called `ble_gatts_start()` — so the Slate and DFU services were advertised but absent from the attribute table. Now registered in `start_stack` before `nimble_port_freertos_init` | **aligned** (N-16) |

Expand this table as further I-9 audits land.

**The TWI row, stated honestly.** This divergence was not decided — it was
never noticed. Slate built a mechanism InfiniTime does not have, under a
standing rule that says mirroring is the default, and the SHORTS bit positions
that mechanism depends on were hand-written and wrong. Every I2C transfer on
the watch failed from bring-up until 5 Aug. The wrong constants were
downstream of an undecided design.

Ratified now, rather than reverted, because:

1. It is what nrfx does. The shortcuts are the vendor-sanctioned path, and the
   constants are now verified against `third_party/nrfx/mdk/nrf52_bitfields.h`
   — the same header the fix was checked against, vendored in this repo.
2. It spins the CPU less. InfiniTime blocks on three separate polling loops
   per transfer; Slate has one bounded wait. Touch reads run on the app task,
   which is the task already implicated in the inbox-drop stalls (P-8 / N-35).
3. Rewriting now would stack an unverified rewrite on an unverified fix. The
   corrected driver has never once completed a transfer on hardware.

**Settled 5 Aug 21:29.** The revisit trigger was "still failing with
`twiStatus = 1`". It did not fire: 244 transfers, 0 failures. The SHORTS design
works, and porting `TwiMaster` would now be a rewrite for its own sake. This
row stays a documented divergence, not an outstanding action.

**The general lesson**, which is what makes this row worth keeping: this is the
second bus driver in the table to diverge (see SPI master, "proportionate
step"). A divergence that is *chosen* and written down is fine. The danger is
the divergence nobody recorded — it carries hand-written constants that no
reference tree will ever contradict, and nothing catches them until hardware
does.

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
| **TWIM SHORTS bits** (5 Aug) | PS / `nrf52_bitfields.h`: LASTTX_STARTRX 7, LASTTX_SUSPEND 8, LASTTX_STOP 9, LASTRX_STARTTX 10 | STOP 8, SUSPEND 7, STARTRX 10 — the block was shifted | **Every** I2C transfer failed since bring-up, on every device. Writes suspended instead of stopping; write_read never issued the repeated START |
| **TWI pin drive** (5 Aug) | `PULL_Disabled` + `DRIVE_S0D1` (open drain) | `PULLUP` + `DRIVE_S0S1` | Wrong, and fixed — but **not** the cause. Fixing it alone changed nothing on hardware |
| **Vibration** (5 Aug) | One-shot FreeRTOS timer | 25–200 ms busy-wait on the app task | Widened the app-task stall behind the inbox drops |
| **Battery power-present pin** (5 Aug) | Reads `PowerPresent` (19) as well as `Charging` (12) | Charge pin only | No `isFull`; a full watch on the charger drifted down |
| **SAADC acquisition time** (5 Aug) | 40 µs | 10 µs | Sample-and-hold does not settle behind the 1:2 divider |
| **Touch sample validity** (5 Aug) | Out-of-range coords or unknown gesture → whole sample invalid | Clamped x/y to 239 | A garbage read became a plausible edge-of-screen tap |

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
| Pin map | `drivers/PinMap.h` | `CLAUDE.md` hardware block — **checked 5 Aug, every pin matches** |
| Touch **sleep/wake** | `Cst816S::Sleep()` / `Wakeup()` | `cst816s::sleep()` / `wake()` — **done 5 Aug** |

## Known gap — **closed 5 Aug**

`Cst816S::Sleep()` / `Wakeup()` now have Slate equivalents, and the bus-wide
teardown they were meant to work around turned out not to need working around:
ENABLE=0 preserves PSEL, FREQUENCY and PIN_CNF, so the per-poll `twi::init()`
was never restoring anything. See "Scoped mirroring task" §2 below — the read
failures were the pin drive setting, not the bus teardown.

## Audit axes (from the N-series post-mortem)

These four are where the defects actually were, so check them first:

1. **Initialisation ordering** — what is configured, and in what order.
2. **Who initiates** — watch or phone, driver or caller.
3. **Worst-case duration inside a callback or ISR.**
4. **Who writes peripheral ENABLE registers** — and what else that costs.

---

## Sleep mode — how InfiniTime does it (researched 8 Aug, owner request)

Read out of the reference tree rather than recalled. File and line references
are to `C:\Users\highj\Documents\Projects\InfiniTime-main`.

### The state machine

`SystemTask` owns it — `SystemTask.h:56`:

```cpp
enum class SystemTaskState { Sleeping, Running, GoingToSleep, AODSleeping };
```

`GoingToSleep` exists because the transition is asynchronous: `DisplayApp` has
to finish dimming and put the panel to sleep before the peripherals can be
powered down. `IsSleeping()` is `state != Running` (`SystemTask.h:99`), so a
wake arriving mid-transition is handled rather than lost.

### What triggers sleep

`DisplayApp` watches LVGL's inactivity timer (`DisplayApp.cpp:228-234`):

| | |
|---|---|
| `GetScreenTimeOut() - 2000` | dim: brightness → `Low` |
| `GetScreenTimeOut()` | push `GoToSleep` to SystemTask |

Note the guard at `DisplayApp.cpp:273`: it only sends `GoToSleep` when its own
message queue is empty, so a wake already in flight is not raced.

### The sleep sequence, in order

1. **DisplayApp** (`DisplayApp.cpp:301-348`): step brightness down to `Low`,
   then `Off`; return to the Clock screen if a transient app is open; **clear
   the touch state** (a stuck "pressed" keeps refreshing the activity timer and
   the screen never sleeps at all); `lcd.Sleep()`; tell SystemTask
   `OnDisplayTaskSleeping`.
2. **St7789::Sleep()** (`St7789.cpp:298`): `SleepIn()` — the `SLPIN` command
   plus a 5 ms settle — then `nrf_gpio_cfg_default(pinDataCommand)` so the D/C
   pin stops sourcing current.
3. **SystemTask**, on `OnDisplayTaskSleeping`: `spi.Sleep()`,
   `spiNorFlash.Sleep()`, and `touchPanel.Sleep()` — the last **skipped** when
   the DoubleTap wake mode is on, because the panel has to stay awake to detect
   it (`SystemTask.cpp:412-415`).

`SpiMaster::Sleep()` (`SpiMaster.cpp:256`) spins until `ENABLE` reads 0, then
returns SCK/MOSI/MISO to `nrf_gpio_cfg_default`. `Spi::Sleep()` does the same
for the CS pin. The point is that a driven pin into a sleeping peripheral leaks.

`Cst816S::Sleep()` (`Cst816s.cpp:106`) is a reset pulse — RST low 5 ms, high
50 ms — followed by writing `0x03` to register `0xA5`.

### The wake sources

All funnel into `SystemTask::GoToRunning()` (`SystemTask.cpp:404`):

- side button
- touch (CST816S IRQ)
- **wrist raise** — `MotionController::ShouldRaiseWake()`
- shake, above a configurable threshold
- **charging event** — `OnChargingEvent` → `GoToRunning` (`SystemTask.cpp:355`)
- new notification, alarm, chime, pairing, timer expiry

Waking reverses the order: `spi.Wakeup()`, `spiNorFlash.Wakeup()`,
`touchPanel.Wakeup()`, then DisplayApp does `lcd.Wakeup()` (`SLPOUT`,
restore scroll address, `DisplayOn`) and re-applies brightness. If the radio is
enabled and unconnected it also restarts fast advertising.

**Charging does not hold the watch awake.** Plugging in wakes it so the change
is visible, and the ordinary inactivity timeout then puts it back to sleep —
which is the behaviour the owner asked for.

### The raise-to-wake algorithm

`MotionController::ShouldRaiseWake()` (`MotionController.cpp:113`) works on
accumulated statistics, not one sample:

```cpp
constexpr uint32_t varianceThresh = 56 * 56;
constexpr int16_t xThresh = 384;
constexpr int16_t yThresh = -64;
constexpr int16_t rollDegreesThresh = -45;
```

Reject if |xMean| is large (arm not level). Reject if yVariance is high — high
variance means real acceleration, and the test wants gravity only. Then require
the roll between the previous and current mean to exceed -45°. There is a
matching `ShouldLowerSleep()` for lower-wrist-to-sleep.

### CPU sleep

`configUSE_TICKLESS_IDLE 1` at `configTICK_RATE_HZ 1024`, `configUSE_IDLE_HOOK 0`
(`FreeRTOSConfig.h:59-80`). There is no idle hook and no explicit `__WFE`: the
Nordic FreeRTOS port's `vPortSuppressTicksAndSleep` does it whenever no task is
runnable. Sleep is inhibited by a wake-lock count, `IsSleepDisabled()` =
`wakeLocksHeld > 0` (`SystemTask.h:82`).

### What this means for Slate

Slate already has `st7789::sleep_in()` / `sleep_out()`, the BMA tilt IRQ
(`Core::poll_tilt`), and a `display_on_` flag that currently does nothing.
Missing: the state machine, the inactivity timeout, the driver sleep helpers
(SPI, touch, flash) and the wake wiring.

**The one Slate-specific hazard, and it is a big one.** `CLAUDE.md`: the app
task loop is the *only* thing that pets the watchdog, and the bootloader dog is
~7 s. InfiniTime can lean on tickless idle because its watchdog handling
differs; Slate cannot adopt `configUSE_TICKLESS_IDLE 1` without the soak that
**I-3** already demands. So the split is:

- **Now:** panel sleep, backlight off, touch sleep, SPI/flash sleep, the state
  machine and the wake sources. This is where the current actually goes — the
  backlight and panel dominate, and none of it touches the app loop's cadence.
- **Deferred to I-3:** `configUSE_TICKLESS_IDLE`, with a soak proving the WDT
  is still fed. Do not bundle it into the same flash.

## Scoped mirroring task (owner request, 5 Aug): battery, sleep/wake, input, vibration

**Status: all four done and verified on hardware, 5 Aug 2026, build
`16D04B4EF949`.**

Getting there took two flashes. `F4A879DE6880` carried all four items plus a
touch fix that **did not work** — `readfail` still tracked `irq` exactly
(`5.5.0.0`). The real cause (TWIM SHORTS constants, §2) went into
`16D04B4EF949`, which reads `244.0.8.6`: 244 transfers, zero failures, taps
landing on hit rects. Battery is confirmed too (§3). The one prediction that
failed is the stall — see the table at the end of this section.

### 1. Vibration — **fixed**

| | InfiniTime | Slate (before) | Slate (now) |
|---|---|---|---|
| Driver | `components/motor/MotorController.cpp` | none — `board::pulse_motor()` | `src/motor.cpp` (`slate::motor`) |
| Pin | `PinMap::Motor = 16`, active low | 16, active low — **matched** | unchanged |
| Timing | FreeRTOS one-shot timer, non-blocking | `board::busy_wait_ms()` on the app task | FreeRTOS one-shot timer |

`main.cpp::do_haptic()` busy-waited up to 80 ms, and DOUBLE busy-waited
25 + 40 + 25 = **90 ms** inline, on the task that also drains the SDP inbox.
`sdp_interpreter.cpp::SideEffectSink` had a second copy of the same busy-wait,
with different durations, reached whenever a phone-pushed list carried a
HAPTIC op — up to 200 ms for its DOUBLE.

Now: `slate::motor::play()` arms a one-shot timer and returns. Both call sites
use it, and the timings live in one table (`motor::pattern_desc`), so a
pattern feels the same however it was raised. The timer is armed *before* the
pin is driven, so a full timer-command queue can never leave the motor
running — same order as `MotorController::RunForDuration`.

DOUBLE has no InfiniTime equivalent (they only ever fire one pulse), so the
multi-pulse sequence is Slate's: one one-shot re-armed alternately for the
burst and the gap. That sequencer is header-only and host-tested
(`tests/host/test_motor.cpp`) — it is the part where a mistake leaves the
motor running.

### 2. Sleep / wake — **fixed, and it found the touch defect**

Two things were wrong, and only the second one mattered.

**TWI teardown.** Slate disabled the whole bus in `power::buses_idle()`, so
`cst816s::poll()` ran a full `twi::init()` before every read on the theory
that PSEL/FREQUENCY/pin config needed restoring. They do not: ENABLE=0
preserves all of it. InfiniTime's `TwiMaster` just toggles ENABLE around each
transaction (`Wakeup()` / `Sleep()`), which is now what `twi::write`,
`twi::read` and `twi::write_read` do. `buses_idle()` calls `twi::sleep()`
instead of writing the register directly.

**The pin drive.** InfiniTime configures SCL/SDA as `DIR_Input | Connect |
PULL_Disabled | DRIVE_S0D1` (so does nrfx_twim). Slate had `PULLUP |
DRIVE_S0S1`, which actively drives the line high and cannot work as an I2C
pad. Corrected — **and it made no difference on hardware.** `F4A879DE6880`
measured `5.5.0.0`: five interrupts, five failed reads, still one for one.
Keep the fix (it is wrong the other way), but it was not the fault.

**The SHORTS bit positions — this is the defect.** `nrf52832_regs.hpp` had the
TWIM SHORTS block shifted against the nRF52832 PS and against Nordic's own
`third_party/nrfx/mdk/nrf52_bitfields.h`, which is vendored in this repo:

| Constant | Slate had | That bit actually is |
|---|---|---|
| `SHORT_LASTTX_STOP` | 8 | LASTTX_SUSPEND (STOP is 9) |
| `SHORT_LASTTX_SUSPEND` | 7 | LASTTX_STARTRX |
| `SHORT_LASTTX_STARTRX` | 10 | LASTRX_STARTTX |
| `SHORT_LASTRX_STOP` | 12 | correct |

So `twi::write` ended each transfer by **suspending** instead of stopping, and
`twi::write_read` never armed the LASTTX→STARTRX short at all — after the
register byte the peripheral simply sat there. Neither ever produced
`EVENTS_STOPPED`, so both died on their timeout, every time. Plain `twi::read`
was the one path with a correct short, and nothing in the firmware uses it.

That is why **no I2C transfer has ever succeeded on this watch**, on any
device, in the whole project: touch, BMA (steps have read 0 throughout) and
the HRS3300 sleep write (so the HR sensor has been powered all along).

**Why it took three flashes.** The symptom — "reads fail" — was attacked three
times (bus wake, full re-init, pin drive) because the counter said only *that*
a read failed, never *how*. A Timeout and an AddressNack are the same number.
Diag line 3's touch group now ends with the `twi::Status` of the last read
(`0` Ok, `1` Timeout, `2` AddrNack, `3` DataNack, `4` BusError). Had that been
there, the first reading would have said Timeout and pointed at the transfer
sequence rather than at the bus.

Also hardened while in here: the touch config buffers are now non-const
locals. EasyDMA can only read Data RAM, and a `const` local array may be
promoted to `.rodata` in flash under `-Os`. Disassembly confirms the current
build puts them on the stack — this just stops that being a compiler decision.

`cst816s::sleep()` / `wake()` now exist, mirroring `Cst816S::Sleep()` /
`Wakeup()`. **Nothing calls `sleep()`.** InfiniTime only sleeps the panel when
no touch wake-up mode is enabled (`SystemTask.cpp`: `if
(!isWakeUpModeOn(DoubleTap)) touchPanel.Sleep()`), and Slate wants tap-to-wake
— so leaving the panel in normal mode *is* the mirrored behaviour. It also
avoids the 0xA5 sleep command that historically wedged the part. The primitive
is there for I-17.

### 3. Battery — **three divergences fixed**

N-12 had aligned the SAADC config and the formula. Remaining:

| | InfiniTime | Slate (before) |
|---|---|---|
| Charge pin pull | `nrf_gpio_cfg_input(Charging, PULL_Disabled)` | PULLUP |
| Power-present pin | `PinMap::PowerPresent = 19`, read every measurement | **not read at all** |
| Acquisition time | `NRF_SAADC_ACQTIME_40US` | TACQ field 2 = **10 µs** |
| Sample cadence | 10 **minutes** + charge-pin edge + once at init | 10 **seconds** + charge edge + init |
| Filtering | none — the direction gate is the filter | same (aligned) |

The power-present pin is the substantive one. InfiniTime reads **both** pins:
`isCharging` says current is flowing, `isPowerPresent` says the cable is in.
They differ exactly when the cell is full, which is how `isFull` is derived —
and the up-only/down-only gate follows **power present**, not charging. Slate
used `charging` for both, so a full watch resting on the charger flipped to
down-only and drifted, and there was a hand-rolled "drop the 99 % clamp after
unplug" hack to compensate. The hack is gone; `isFull` replaces it.

10 µs acquisition is below what the datasheet allows for the 1:2 divider's
source impedance — the sample-and-hold never finishes charging, so readings
come in low. Now 40 µs, costing 30 µs once every 10 s.

**Deliberate divergence kept:** the 10 s sample cadence against InfiniTime's
10 min. Diag line 2 shows live millivolts and that is still being used for
bring-up. Revisit when the overlay goes.

### 4. Input — **gesture decoding mirrored; button path is a documented divergence**

`TouchHandler::ProcessTouchInfo` does two things Slate did not:

- **Validity is all-or-nothing.** An out-of-range coordinate or an unknown
  gesture byte invalidates the whole sample. Slate *clamped* x/y to 239, which
  turns a garbage read into a plausible tap at the screen edge.
- **One gesture per touch.** A slide or long-press is accepted only while a
  finger is actually on the panel, and not again until the panel reports
  release. The controller keeps returning the same gesture byte while the
  finger is down, so without the latch one slide fires repeatedly.

Both are now in `cst816s::poll()` + `input_event.cpp`. `TouchSample` gained
`touching` (InfiniTime `TouchInfos::touching`), and `valid` now means what
`isValid` means — the read succeeded and the data is sane, *not* "a touch
happened".

**Button — divergence, deliberate.** InfiniTime drives the button from a
GPIOTE interrupt into a timer-based state machine (`ButtonHandler`); Slate
polls with a majority-vote debounce on the app task (`button.cpp`, 20 ms
debounce / 500 ms long-press / 350 ms double window). Different mechanism,
same actions out, and the polled version is what makes the WDT hold-to-reset
path work when higher layers are wedged. Button navigation is the one input
path that has always worked on hardware — not worth churning.

### Verified on hardware, 5 Aug 21:29 (`16D04B4EF949`)

| Reading | `F4A879DE6880` | `16D04B4EF949` | Verdict |
|---|---|---|---|
| line 3 touch group | `5.5.0.0` | **`244.0.8.6`** | **Fixed.** 244 interrupts, 0 read failures, 8 touches, 6 hits |
| battery | 4096 mV / 93 % | **4190 mV / 99 %** | 40 µs acquisition reads ~94 mV higher, as predicted; 99 % is the InfiniTime charging clamp working |
| line 1 `stall` | 878 ms | **887 ms** | **Unchanged — the haptic fix was not the cause.** See below |
| steps | `0` | `0` | Inconclusive; the watch was stationary. The BMA is at least reachable now |

**The stall prediction was wrong, and worth recording as wrong.** Removing up
to 200 ms of busy-wait from the app task moved `stall` by 9 ms in the wrong
direction. Line 2 says why: worst phase is **3** (session/core tick) at 444 ms
with a 222 ms render. The stall is the **face repaint**, not vibration. The
haptic change is still correct — it is what InfiniTime does, and it removed
real blocking — but it was never the dominant cost, and claiming it would be
was an inference, not a measurement. Filed as N-36.

If `readfail` still tracks `irq`, **read the last field before theorising**:

- `1` Timeout — the transfer still never completes. Suspect the sequence, not
  the bus: SHORTS, PSEL, or the peripheral not actually enabled.
- `2` AddressNack — the bus works and the slave is not answering. Now the pin
  drive, pull-ups and the controller's own state are in scope, and
  `cst816s::wake()` (hard reset + reconfigure, off the poll path) is the
  mirrored recovery to try.
- `3` DataNack / `4` BusError — addressing works, the transfer breaks
  mid-way; look at FREQUENCY and rise times.
