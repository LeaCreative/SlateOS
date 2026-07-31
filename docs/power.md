# Power policy (M14 / §8)

Targets: **ambient <200 µA avg**, **active session 20–25 mA**.  
Gate C hard-fail: ambient >250 µA or active >30 mA.

## What changed

1. **Tickless / WFE idle** — FreeRTOS uses RTC1 tickless idle (`port_rtc_tick.c`);
   see `docs/freertos-tickless.md`. SysTick cannot wake from System ON sleep on nRF52.
2. **Peripheral discipline** — SPIM0/TWIM1 disabled when idle; SAADC `TASKS_STOP` after sample; HRS3300 `PDRIVER=0` at boot; XT25 deep power-down; ST7789 `SLPIN` when backlight off; PWM stopped at 0%.
3. **Button strobing** — already strobes P0.15 (was not left high).
4. **Pin-sense** — CST816S IRQ uses `PIN_CNF SENSE` + GPIOTE **PORT** (not edge `IN`).
5. **Conn interval** — renegotiated on every profile/power transition (`ble::request_conn_interval`). Ambient catalog interval = **500 ms**.
6. **Debug** — see below; ~3 mA if left enabled after SWD disconnect.

## Instrumentation build

CMake `-DSLATE_POWER_INSTRUM=ON` (default ON). RTT lines:

```
PWR t=0x........ ambient->active
```

`t` is TIMER0 microseconds. Align a current trace to these markers.

## Debug peripheral OFF (Gate C)

The SWD debug block can stay enabled after the debugger disconnects until a **power cycle**, costing ~**3 mA**.

**Procedure:**

1. Flash the firmware.
2. **Fully disconnect** SWD (unplug ST-Link / nRF52832 DK debugger).
3. **Power-cycle the watch** (battery disconnect/reconnect or charge plug cycle). Soft reset is not enough.
4. Do **not** attach RTT/GDB during the Gate C capture (or accept that the number is invalid).
5. Optional: `nrfjprog --pinreset` after unplugging still may leave debug powered — prefer battery power-cycle.
6. Soft assist in firmware: `power::disable_debug_assist()` → `TASKS_LOWPWR` (does **not** replace the power cycle).

## Measuring without opening the housing

You said you likely **cannot** use a PPK II without opening the case. Options:

| Method | Notes |
|---|---|
| **PPK II on battery leads** | Requires access to the cell / charge path — usually means opening or a broken-out harness |
| **External charge contacts** | If accessible, series-measure charge current ≠ true battery current when running on cell |
| **SWD VTref / DK** | Not valid for Gate C (debugger current + different supply path) |
| **Budget model** | Use the table below until a harness exists |

This delivery **does not claim lab-measured µA**. The table is a **budget model** from §8 references + expected deltas of the changes above. Fill the “Measured” column when you have a probe path.

## Current budget table (§8 + M14 deltas)

| State | Backlight | Conn interval | Budget (est.) | Measured | Notes |
|---|---|---|---|---|---|
| **Ambient** | off + LCD SLPIN | 500 ms | **<200 µA** target (~66–150 µA if sleep+radio OK) | *TBD* | Hard fail >250 µA |
| **Glance** | low | unchanged | ~10 mA | *TBD* | Seconds |
| **Active** | mid | 30 ms | **20–25 mA** | *TBD* | Hard fail >30 mA |
| **Streaming** | mid/high | 15 ms | ~25–30 mA | *TBD* | Time-boxed |

### Component deltas (order-of-magnitude)

| Item | Approx. impact if left wrong |
|---|---|
| **Main-loop busy-wait (pre-M14)** | CPU never sleeps → **mA** floor (single largest firmware bug for ambient) |
| **Debug SWD left on** | **~3 mA** |
| **Edge GPIOTE vs pin-sense** | up to **~0.47 mA** |
| **Button enable left high** | **~34 µA** (strobing already avoids this) |
| **Conn interval 30 ms vs 500 ms** | radio duty → **mA-scale** in connected ambient |
| SPI/TWI left enabled | tens–hundreds µA |
| LCD not in SLPIN | ~mA vs sleep |
| SAADC not STOPPED | residual µA |

## Which single item moved the needle most?

**Replacing the main-loop `busy_wait_ms(5)` with LFXO/RTC `WFE` idle.**

Without that, the Cortex-M4 stays in run mode continuously and ambient **cannot** reach <200 µA no matter how carefully peripherals are gated. Debug-off is a close second **if** you leave a probe attached (~3 mA alone fails Gate C). For a correctly power-cycled, disconnected watch, **connection-interval stretch to 500 ms in ambient** is the next largest connected-mode lever.

## Build

```powershell
$env:Path = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin;" +
  "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;" +
  "C:\Program Files\CMake\bin;" + $env:Path

cmake -S . -B build/power -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DSLATE_POWER_INSTRUM=ON
cmake --build build/power
```

Artifacts: `build/power/slate_firmware.{elf,hex,bin}`