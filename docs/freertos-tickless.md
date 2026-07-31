# FreeRTOS tick and tickless idle

When FreeRTOS is linked (`SLATE_HAS_NIMBLE=1` or bisect FreeRTOS),
`config/FreeRTOSConfig.h` sets:

- `SLATE_FREERTOS_RTC_TICK = 1`
- `configUSE_TICKLESS_IDLE = 0` for now (RTC sleep path fixed but not
  soak-tested; first enable reverted at ~7 s via a tick catch-up underflow)
- `configTICK_RATE_HZ = 1024`
- `configSYSTICK_CLOCK_HZ = 32768` (LFXO; RTC1 PRESCALER = 31)
- `configTOTAL_HEAP_SIZE = 16 KB` (14 KB exhausted NimBLE `xQueueCreate` →
  NULL eventq → `queue.c:1513` cyan assert on sealed watches; bisect uses 18 KB)

Tick ISR: **RTC1** (`port/nrf52/src/port_rtc_tick.c`), InfiniTime / Nordic pattern.
SysTick is not used — on nRF52 the core clock is gated in System ON sleep, so a
SysTick-driven tickless idle never wakes (silent ~7 s bootloader WDT revert).

| Peripheral | Owner |
|---|---|
| RTC0 | NimBLE `os_cputime` / PHY |
| RTC1 | FreeRTOS tick + `wall_clock` / `rtc_hw` (1024 Hz COUNTER) |
| RTC2 | unused |
| TIMER1 | `board::micros()` / busy-wait |

Tickless idle stops RTC1 TICK interrupts, programs COMPARE0 for the wake time,
clears FPU sticky IRQs (errata 87), then `WFE`. Sleeps are capped at 1 s so the
bootloader watchdog is always reloaded from `configPOST_SLEEP_PROCESSING`.

## Bare-metal (no FreeRTOS)

`rtc_hw` starts RTC1 itself at 32768 Hz. `power::sleep_ms` falls back to
`board::busy_wait_ms` (TIMER1).

## Do not

Do not re-enable tickless idle with the stock SysTick CM4F port — the config
`#error`s that combination. Absolute µA targets need a sealed-watch discharge
curve (§8), not a PPK.
