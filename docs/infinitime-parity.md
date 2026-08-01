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
