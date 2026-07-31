# Firmware update (M15) — OTA, MCUBoot, recovery

Sealed PineTime has **no accessible SWD**. A bad update is an unrecoverable brick
unless the defenses below hold. Read this before shipping any image.

**Sealed install (InfiniTime → Slate):** see [`docs/flash-sealed.md`](flash-sealed.md).

## Flash layout (InfiniTime MCUBoot contract)

### Internal flash (512 KiB)

| Region | Address | Size | Notes |
|---|---|---|---|
| MCUBoot | `0x00000000` | `0x8000` (32 KiB) | Bootloader (+ reserved log) |
| PRIMARY slot | `0x00008000` | `0x74000` (475136) | App MCUBoot image |
| Scratch | `0x0007C000` | `0x1000` (4 KiB) | MCUBoot swap scratch |
| Spare | `0x0007D000` | `0x3000` (12 KiB) | Unused by BL |
| App persist | `0x0007F000` | `0x1000` (4 KiB) | Clock / alarms / settings |

Authoritative constants: [`include/flash_map.hpp`](../include/flash_map.hpp).  
Linker default: **`BOOTLOADER_PRESENT=ON`** (origin `0x8020`, length `0x73FE0` — after 32B MCUBoot header).

### External SPI NOR (4 MiB)

| Region | Address | Size | Notes |
|---|---|---|---|
| Recovery / assets | `0x00000000` | `0x40000` (256 KiB) | Bootloader recovery image |
| SECONDARY slot | `0x00040000` | `0x74000` (475136) | Channel-5 OTA + Nordic DFU (InfiniTime offset) |
| LittleFS | `0x000B4000` | `0x34C000` (~3.3 MiB) | Assets / logs |

**Breaking change:** LittleFS moved from `0x80000` to `0xB4000`. First boot after this
map **formats** the new LFS region — phone must re-push assets.

## Architecture

```
Phone ──SDP ch5──► App OTA receiver ──write──► SECONDARY @0x40000
Phone ──legacy DFU► App DFU GATT     ──write──► SECONDARY + InfiniTime magic
                         │
                    reboot / reset
                         ▼
                   MCUBoot swap → PRIMARY (test)
                         │
              BLE session up → IMAGE_OK (confirm)
                         │
              else ≤ N=3 boots → revert
```

MCUBoot itself is **out-of-tree** (InfiniTime pinetime-mcuboot on sealed units).
This repo owns the map, packaging, download paths, confirm-on-BLE, and WDT petting.

## Confirm = a central held the link for 10 s

`N = 3` (`flash_map::kConfirmBootLimit`).

After a swap, the new image runs as a **test** image. The app loop feeds
`ble::central_connected()` into `slate::boot::tick_confirm()`; once a central has
held the connection for `boot::kConfirmDwellMs` (10 s), that writes MCUBoot
`IMAGE_OK`. While unconfirmed, the local face draws an **amber bar** across the
top so the state is visible on the watch instead of being silent.

Any central qualifies — companion app, nRF Connect, Gadgetbridge. It is
deliberately **not** gated on a completed Slate HELLO handshake: requiring that
meant the user had to finish Android permissions and CDM pairing before the image
was safe from the next reset, which is an unreasonable amount of work to complete
under threat of rollback.

Bare boot / advertising alone does **not** confirm, and there is **no** timer or
button confirm — either would permanently stick a radio-broken image (InfiniTime
BL degraded-case table) with no way back in on a sealed watch, since every
recovery path is a DFU push over BLE. A connection is the cheapest evidence that
the radio, host and GATT server a future DFU needs are all alive. Firmware that
boots but never accepts a connection reverts after ≤3 resets.

Note that only one central can be connected at a time
(`MYNEWT_VAL_BLE_MAX_CONNECTIONS = 1`). A DFU tool left auto-reconnecting will
hold that slot and starve the companion app; see `docs/flash-sealed.md`.

**Watchdog:** InfiniTime MCUBoot starts the NRF WDT (~7s) before jumping to the
app. Slate mirrors InfiniTime `SystemTask`: `app_loop` calls
`slate::wdt::pet_service()` each iteration (~20 ms / ambient ~200 ms).
`wdt::pet()` reloads RR0 only while the side button is **up**. Tick and idle
hooks do **not** pet — a wedged app task starves the dog within ~7 s. Hold the
button ~7 s → WDT reset even while the loop runs. `configPOST_SLEEP_PROCESSING`
→ `slate_wdt_pet` remains for future tickless sleeps (capped 1 s). Full-slot
erases pet inside `ota_slot::erase_all` / `xt25::wait_ready`. Fatal paths call
`slate::wdt::fatal_starve()` — never pet forever. See `docs/infinitime-parity.md`.

## Channel 5 OTA (Slate companion)

**Status: fully implemented** — firmware (`src/ota_xfer.cpp`) and companion
(`SlateOtaService`, `SlateOtaActivity`, codec in `sdp-core/slate/ota/OtaXfer.kt`).

Ops (mirrors ASSET): `BEGIN` / `CHUNK` / `ABORT` / `COMMIT` + `CREDIT` / `ACK` / `NAK`.

`BEGIN` payload: `id:u16`, `total:u32`, `sha256[32]`, `version:u32` (43 bytes total).
`CHUNK` header: `op`, `id:u16`, `offset:u32` (7 bytes + data, max 512 B data).
`ACK`: `op`, `id:u16`, `received:u32` (7 bytes).
`CREDIT`: `op`, `credit:u16` (3 bytes, initial window = 2048 B).

- Refuse if battery **&lt; 30%** and not charging → `NAK LowBattery`.
  Percent is **0** until the first SAADC sample (sampled at `battery_hw::init`).
- Resume: same `id` → `ACK` with current `received` offset.
- Credit refill when window drops below 25% (firmware sends new `CREDIT`).
- `COMMIT`: verify SHA-256, write InfiniTime pending-image magic, reboot into MCUBoot swap.
- After COMMIT the companion waits 3 s for reboot, then link service reconnects normally.
  Holding that reconnection for 10 s writes MCUBoot `IMAGE_OK` (see above).

## Nordic legacy DFU (independent path)

GATT service `00001530-1212-EFDE-1523-785FEABCD123` (control `…1531`, packet `…1532`,
DFU revision `…1534` read → `0x0008`).

Registered **alongside** the Slate service when NimBLE is linked. Does **not** use
SDP. nRF Connect / Gadgetbridge / Amazfish can push a DFU zip into SECONDARY, then
Activate&Reset → MCUBoot swap.

Wire sequence (InfiniTime/SDK 7.x / Adafruit nrfutil compatible):

1. Phone writes `0x01 0x04` (Start, App type) on Control — **no response yet**.
2. Phone writes 12-byte size packet `[sd:u32=0][bl:u32=0][app:u32]` on Packet.
   Watch erases SECONDARY slot and sends `10 01 01` (Start response).
3. Phone writes `0x02 0x00` on Control (Init start); phone writes raw `.dat` bytes on Packet;
   phone writes `0x02 0x01` on Control (Init complete) → Watch sends `10 02 01`.
4. Phone optionally writes `0x08 <interval>` (PRN) → Watch sends `10 08 01`.
   At every `interval` firmware packets the watch sends `0x11 <received:u32 LE>`.
5. Phone writes `0x03` (Receive FW) → Watch sends `10 03 01`.
6. Phone streams image in 20-byte Packet writes (WRITE_NO_RSP).
7. Phone writes `0x04` (Validate) — Watch checks **CRC-16-CCITT** of all received bytes
   against the last 2 bytes of the init packet → `10 04 01` or `10 04 05`.
8. Phone writes `0x05` (Activate & Reset) → Watch writes InfiniTime pending-image magic,
   sends `10 05 01`, then calls `system_reset()`.

Same 30% battery gate as channel 5. On Validate, writes InfiniTime DFU magic words
at the end of the secondary slot. Disconnect at any stage resets all state for the
next connection (no stale pending state).

### Pack a DFU zip (sealed / InfiniTime BL)

```powershell
python scripts/package_dfu.py --bin build/dfu/slate_firmware.bin --version 1.0.0
# or: cmake --build build/dfu --target slate_dfu
```

### Pack a signed image (custom BL / SWD factory only)

```powershell
python scripts/sign_firmware.py --bin build/power/slate_firmware.bin --version 1.0.0 --key path\to\slate_priv.pem
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application slate-signed.bin slate-dfu.zip
```

## ECDSA-P256 signing

See [`bootloader/keys/README.md`](../bootloader/keys/README.md). Used when building
against a **custom** MCUBoot that validates Slate’s key — **not** required for
stock InfiniTime bootloader (`imgtool create` is enough).

```powershell
imgtool keygen -k slate_priv.pem -t ecdsa-p256
imgtool getpub -k slate_priv.pem > bootloader/keys/slate_pub.pem
python scripts/gen_pubkey_c.py
python scripts/sign_firmware.py --bin build/power/slate_firmware.bin --version 1.2.0 --key path\to\slate_priv.pem
```

Public key is embedded for MCUBoot verify config and app sanity. **Private key
never enters this repository.**

## Recovery procedures

| Failure mode | What to do |
|---|---|
| **Bad / unsigned image** | MCUBoot refuses or reverts. Re-flash via DFU or (devkit) SWD. |
| **Interrupted transfer** | Secondary not marked complete; reboot keeps old PRIMARY. Resume OTA with same `id`, or Abort and restart. |
| **Boots but cannot connect** | Do nothing for ≤3 boots — MCUBoot reverts. Or hold button into bootloader recovery. |
| **WDT not petted** | Reset within ~7s → revert. Use `slate::wdt::pet_service`. |
| **Corrupted bootloader** | **No field recovery** on a sealed watch. Factory SWD only. Never OTA the bootloader; enable APPROTECT after factory flash. |

## Brick enumeration — every way, and the defense

| How it bricks a sealed device | Defense |
|---|---|
| Truncated / bit-flipped OTA payload | SHA-256 on `COMMIT`; no reboot on mismatch |
| Power loss mid-OTA | Resume; swap only after successful `COMMIT` / DFU Validate |
| New image boots, BLE stack broken | No `IMAGE_OK` → revert within N=3 boots (must not pet forever) |
| Slate SDP / companion broken, BLE OK | Nordic legacy DFU → same SECONDARY → swap |
| App never starts (hard fault / WDT) | Bootloader revert or button → recovery image with DFU |
| Bootloader itself corrupted | **Undefended in field** — mitigate: never OTA BL, APPROTECT, factory test |
| Update at &lt;30% battery | OTA/DFU refuse `BEGIN` / Start; ADC sampled before gate |
| Secondary overwrite of LittleFS | Fixed external map; LFS starts at `0xB4000` |
| Confirm on bare boot (false permanent) | Confirm **only** from `tick_confirm()` after a central holds the link 10 s |
| Confirm gate too strict to reach | Any central counts, not just a Slate HELLO session; no deadline while the watch stays up |
| SoftDevice / PineDFU path | Companion preflight **blocks** Slate MCUBoot zip |
| Fatal hook pets WDT forever | `fatal_starve()` / button-gated `pet_service()` |

## SRAM budget

Total SRAM: 64 KiB. Hard gate: static + heap ≤ 54 KiB, ≥ 6 KiB slack.

Largest consumers (production build):

| Symbol | Size | Module |
|---|---|---|
| `ucHeap` | 14336 B | FreeRTOS heap (`configTOTAL_HEAP_SIZE`) |
| `g_interp` | ~8600 B | SDP interpreter |
| `g_renderer` | ~7700 B | Renderer + tile buffers |
| `g_page_cache` | 4096 B | Persist NVM page cache |
| NimBLE pools | ~5500 B | ACL, event, HCI buffers |

BLE ISO (LE Audio) is **disabled** in `port/nrf52/include/slate_syscfg.h`
(`BLE_TRANSPORT_ISO_COUNT=0`) since nRF52832 lacks LE Audio controller support.
This recovers ~3.5 KB vs NimBLE defaults.

**Reading the linker report correctly:** the MSP stack (`SLATE_STACK_SIZE`, 4 KB)
is placed at `ORIGIN(RAM) + LENGTH(RAM)` and is **not** included in the reported
"Used Size". True slack is `65536 − reported − 4096`.

| Build | Reported used | True slack |
|---|---|---|
| Before ISO fix | 61224 B | 216 B |
| After ISO fix | 57736 B | 3704 B |
| + production `boot_diag` | 58216 B | 3224 B |

Slack is still **below the documented 6 KB target**. The FreeRTOS heap cannot be
raised until more static RAM is reclaimed (largest remaining targets: `g_interp`
~8.6 KB, `g_renderer` ~7.7 KB, `g_page_cache` 4 KB).

## Fatal diagnostics on a sealed watch

No SWD, no RTT. `src/boot_diag.cpp` is compiled into **every** build so a fatal
fault paints a full-screen colour, holds ~15 s, then releases the WDT (MCUBoot
reverts the unconfirmed image, so the watch returns to InfiniTime by itself).

| Colour | Cause |
|---|---|
| Red | FreeRTOS malloc failed / `xTaskCreate` failed (heap exhausted) |
| Orange | FreeRTOS stack overflow |
| Cyan | `configASSERT` |
| Magenta | HardFault — CFSR / HFSR / PC drawn in hex |

A **silent** black screen followed by a revert means none of these fired: suspect
a fault before the display is up, or a reset from outside the app.

Cyan additionally draws two hex rows: `FNV-1a(basename(__FILE__))` then the line
number. Decode the first row with:

```powershell
python scripts/assert_hashes.py 3B7DE24E   # -> tasks.c
```

Magenta draws CFSR, HFSR and PC in that order.

## Build

```powershell
cmake -S . -B build/power -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/power
cmake --build build/power --target slate_dfu
```

Host OTA/DFU tests:

```powershell
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests
ctest --test-dir build/host-tests -R ota_xfer
```
