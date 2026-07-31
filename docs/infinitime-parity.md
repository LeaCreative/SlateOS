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
| **GATT → UI work** | Callbacks `PushMessage` to SystemTask / DisplayApp; no render in BLE host | Host: reassemble + `AppInbox` copy; app task `drain_app_messages` → session/interp/SPI; CREDIT after deferred apply | **aligned pattern** (N-1 / I-10 stage 1; single 4 KiB slot ≈+4.1 KiB) |
| **Flash / MCUBoot map** | Primary 475136, secondary SPI `@0x40000` | Same InfiniTime contract (`flash_map.hpp`, `imgtool --slot-size 475136`) | **aligned** |
| **RTC ownership** | RTC0 NimBLE; RTC1 FreeRTOS tick | Same (`SLATE_FREERTOS_RTC_TICK`) | **aligned** (tickless still off — I-3) |
| **SPI master** | IRQ-driven `SpiMaster`, shared display+flash CS | Busy-wait SPIM0 with **bounded** `EVENTS_END` wait (5 ms/chunk), recover + bool errors, always-on CS discipline checks (`src/spi_bus.cpp`) | **proportionate step** — escalate to full InfiniTime SpiMaster if behaviour keeps diverging |

Expand this table as further I-9 audits land.
