# Bare-Metal Bring-Up

## Why Bare CMSIS-Style Instead Of nRF5 SDK
- `nRF5 SDK` gives you startup files, drivers, and headers quickly, but it also brings a large HAL surface and default behavior that obscures early bring-up.
- `Bare CMSIS-style` bring-up keeps the project small and makes each register write explicit, which matches the current goal better.
- Trade-off: more low-level code is ours to maintain. That is acceptable for M0 because we only need reset, clock, GPIO, UICR, RTT, and image generation.

## Configure
```powershell
cmake -S . -B build/debug -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBOOTLOADER_PRESENT=ON
```

Default is `BOOTLOADER_PRESENT=ON` (app at `0x8020` after MCUBoot’s 32-byte header; InfiniTime primary slot).
Use `-DBOOTLOADER_PRESENT=OFF` only for bare SWD bring-up without a bootloader.

## Build
```powershell
cmake --build build/debug
```

Outputs:
- `build/debug/slate_firmware.elf`
- `build/debug/slate_firmware.hex`
- `build/debug/slate_firmware.bin`
- `build/debug/slate_firmware.map`

## Bootloader Option
- `BOOTLOADER_PRESENT=OFF`:
  - Flash origin = `0x00000000`, full chip minus persist
  - Early standalone bring-up only
- `BOOTLOADER_PRESENT=ON` (default):
  - Flash origin = `0x00008020` (slot `0x8000` + 32B MCUBoot header), length = `0x73FE0`
  - Requires InfiniTime MCUBoot in the first 32 KiB — see `docs/ota.md` / `docs/flash-sealed.md`

## Memory Layout
- `.text` / `.rodata` live in flash.
- `.data` is stored in flash, copied into RAM by `Reset_Handler`.
- `.bss` is zero-initialized by `Reset_Handler`.
- Heap is a fixed `0x800` byte reserve after `.bss`.
- Stack is a fixed `0x1000` byte reserve at the top of RAM.
- The linker asserts if heap and stack collide.

## UICR / NFCPINS Note
- `SystemInit()` writes `UICR->NFCPINS = 0xFFFFFFFE` when needed so `P0.09/P0.10` become GPIO.
- UICR is non-volatile configuration memory, so the write goes through the NVMC:
  1. wait for `NVMC->READY`
  2. set `NVMC->CONFIG = WEN`
  3. write `UICR->NFCPINS`
  4. restore `NVMC->CONFIG = REN`
- The new pin routing is latched only after a reset that power-cycles the pin logic. On Nordic parts that means a full power cycle after programming UICR, not just a soft reset.

## Flash With OpenOCD And ST-Link V2
```powershell
openocd -f openocd/pinetime_stlink.cfg `
  -c "program build/debug/slate_firmware.hex verify reset exit"
```

## RTT Log Levels
- Debug builds define `SLATE_LOG_LEVEL=4`
- Release builds define `SLATE_LOG_LEVEL=2`

Level meanings:
- `1` error
- `2` warn
- `3` info
- `4` debug

## Watchdog (sealed / MCUBoot)

InfiniTime MCUBoot arms the NRF WDT (~7 s) before jumping to the app. Slate
feeds it like InfiniTime’s `SystemTask`:

- **Pet from the app task** (`pet_service` once per `app_loop` iteration).
- **Do not pet from tick or idle hooks** — a wedged app must starve the dog.
- **Withhold** reload while the side button is held (`wdt::pet()`).
- Long secondary-slot erases also pet inside `ota_slot` / `xt25`.
- Future tickless: `configPOST_SLEEP_PROCESSING` → `slate_wdt_pet` (sleeps ≤ 1 s).

Policy table: `docs/infinitime-parity.md`. Recovery if a bad image pets forever:
`docs/recovery-sealed.md`.
