# Flash Slate onto a sealed PineTime

Sealed watches have **no SWD**. Install Slate the same way you install InfiniTime:
Nordic legacy DFU while InfiniTime (or InfiniTime recovery) is running. MCUBoot
(already on the device) swaps the image. See also `docs/ota.md`.

**Soft-brick (Slate up, no BLE, button hold does nothing):** do not keep retrying
CDM — follow [`docs/recovery-sealed.md`](recovery-sealed.md) (discharge →
pinecone blue/red → InfiniTime → fixed Slate).

| `slate-dfu.zip` SHA-256 prefix | Role |
|---|---|
| `CC3F25A12AB2` | **Bad** — tickless RTC tick; ~7 s InfiniTime revert |
| `790822E97A78` | **Bad** — WDT always petted from tick; no BLE, hold dead |
| `8B2D9054E9E7` | Superseded (2026-07-31 morning) — WDT withhold + RTC tick, tickless off; predates N-1/N-4/N-6/N-8 |
| `D7E86854B9D3` | Superseded (2026-07-31) — first N-4/N-6/N-8 build; boots + paints but radio silent (N-9), no BLE diag readout |
| `2284BA304B8F` | Superseded (2026-07-31) — added BLE telemetry; on hardware read `93.21` = adv_start BLE_HS_ENOADDR (no identity address — N-9 root cause) |
| `4324FC495E0C` | Superseded (2026-07-31) — N-9 fixed (advertising verified on hardware) but watchdog reboot loop every ~30 s (N-10) |
| `E95E6CD9676B` | Superseded (2026-07-31) — catch-up alone; painted cyan `397105A1`/`37A` = `port.c:890`, exposing N-11 |
| `C3AE3F15E408` | Good (2026-07-31) — N-10 + N-11; **first confirmed durable install** (amber cleared, `IMAGE_OK`) |
| `E54859689936` | Good (2026-07-31) — N-12 battery fix verified (3895 mV); its instrumentation diagnosed N-13 |
| `DFD04D130924` | Superseded (2026-08-01) — N-13 fix, but exposed N-14: torn face and unmaintainable link on reconnect |
| `D98B9C174EB1` | Superseded (2026-08-01) — N-14 fixed the tearing; link still dropped after MTU 247 (N-15) |
| `10F1FEF97FD8` | Superseded (2026-08-01) — N-15 got MTU to 247; exposed N-16 (“Slate service not found”) |
| `F27CF0DD8D5A` | Superseded (2026-08-01) — N-16 registration fix; confirmed `IMAGE_OK` and association, but the phone still reported "service not found" (stale Android GATT cache suspected) |
| `0B8109A23AFA` | Superseded (2026-08-01) — GATT self-check + sticky failure codes |
| `FAE65071E5A7` | Superseded (2026-08-01) — fixed `slate_uuids.hpp` only; `ble_nimble.cpp` had its own hardcoded copy, so the wire was unchanged |
| `B7A58CFAC9B6` | Good (2026-08-01) — N-17 fixed; **SDP link verified end-to-end** (MTU 247, service correct, ready to push) |
| `B875E4F60ADC` | Good (2026-08-01) — N-18 tile-band culling; repaint 611 ms → 199 ms, verified |
| `EFBBB8606B39` | Superseded (2026-08-01) — I-13 prep; the first OTA run stalled at 512 B (N-19) |
| `66D6CB890F74` | Superseded (2026-08-02) — credit re-advertised on ACK, but the window was still 4x the link's capacity |
| `47A3FA74D258` | Superseded (2026-08-02) — N-19 lock-step window, but the slot erase still blocked both update paths |
| `7CBF667524F2` | Superseded (2026-08-02) — N-20 lazy erase; upload then ran at 71 kB/s but failed at 11 % (N-21) |
| `9B814EBDE85B` | Superseded (2026-08-02) — N-21 SPI mutex fix; the transfer then completed but hung silently at 100 % (N-22) |
| `AFECC55F505D` | Good (2026-08-03) — N-22 app task stands back during transfers; flashed successfully from InfiniTime |
| `EB591620B5B0` | Good (2026-08-03) — N-23 session starts on TX subscribe; **first image delivered by companion SDP OTA**, confirmed IMAGE_OK |
| `E43C26CBE325` | Good (2026-08-03) — N-25 time-sync retry; watch shows its version and live OTA progress; clock ran in UTC |
| `CFEDE90DE7F4` | Good (2026-08-03) — N-26: TIME_SYNC carries local wall-clock, so the face shows local time |
| `5904578362CC` | **Untested (2026-08-03)** — N-27 return-to-face + TestApp round-trip (P-1). Pair with companion `0.1.0-m16` TestApp build |

**Flashing this one needs a working DFU.** The stall it fixes is what breaks
DFU, so use the bootloader route if nRF Connect keeps disconnecting: hold the
button through the pinecone to **red** for recovery firmware, or **blue** to
roll back, then flash from there.

Full good SHA-256:
`CFEDE90DE7F41212AC57F4676E1AA8ABB68D8A89C625243F3DCF01AB4E9A9779`.

**Slate→Slate SDP OTA is the primary update path since 3 Aug** (I-13, proven
twice on hardware). Booting to InfiniTime to flash is now a fallback, not the
routine, and hold-to-blue remains the guaranteed recovery.

Since `E43C26CBE325` the running image identifies itself: the watch face
carries a version line (`0.1.0-m19 Aug 18 HH:MM` style) in **Face diag On**
builds. From `0.1.0-m19` (`2D7C272691C8`) that line hides with Face diag Off
on the watch or in the companion. Current packaged zip: `build/dfu/slate-dfu.zip`.

**Flashing from InfiniTime is the reliable path** while the watch still runs a
build without N-20/N-21/N-22. Those fixes are what make Slate a dependable DFU
*target*; a build lacking them cannot be relied on to receive its replacement.

Overlay gains a third line during OTA: `<percent>/<last NAK>/<NAK count>`
(NAK 8 = image still on trial). See `docs/ota-verification.md`.

Verify after flashing: nRF Connect should show the 128-bit service as
`e979acfb-c338-0000-a962-e96e4cf078f3`. If it still reads `f378f04c-…`, use
the app's ⋮ → refresh-cache first; nRF Connect caches tables per address too.

**If a central reports "service not found":** Android caches the GATT
database per device address. Forget the watch in Bluetooth settings, remove
the CDM association, then re-pair — otherwise the phone keeps serving the
cached table. Check the overlay first: BLE state **96** means the service
really is missing on the watch; state **7** means it is present and the
problem is phone-side.

**This bites on every InfiniTime ↔ Slate transition.** The device address is
derived from FICR and is therefore identical across both firmwares, while
the service tables are completely different. So after any flash that swaps
firmware, the phone's cached table describes the *previous* firmware. Clear
it **after** the flash and before connecting, not just once at the start:

1. Watch on InfiniTime → clear cache → flash the Slate zip over InfiniTime's
   DFU service.
2. Watch reboots into Slate (**new service table, same address**) → clear the
   cache again → then associate / connect.

Skipping step 2 makes a perfectly good Slate image look like it has no
services. Hold-to-blue at the bootloader reverts to the previous image even
when the current one was validated, so a bad flash is always recoverable.

Second diag line:
`<worst loop phase>.<ms>/<adc raw>/<battery mV>/<parse ms>.<render ms>`.
Phases: 1 drain, 2 input, 3 session/core tick, 4 paint, 5 battery, 6 the
notify wait (6 = the app task was starved, not slow), 7 link transition.

Cyan screen = `configASSERT`. Top hex is FNV-1a of the source basename, bottom
is the line number; decode with the helper in `src/boot_diag.cpp` (`file_hash`).
`397105A1` = `port.c`.
Diag overlay format:
`reset/uptime s/paints/button/worst stall ms/BLE state.rc/recovered ticks`.
BLE state 7 = advertising; 1–6 = bring-up stage reached (see
`ble::bringup_snapshot` in `include/ble_gatt.hpp`); 90–95 = failure at that
stage with `rc`. Reset reason is the raw RESETREAS bitmask and **accumulates**
(2 = watchdog, 4 = soft). Recovered ticks = FreeRTOS ticks the RTC1 catch-up
put back (N-10); healthy paints ≈ uptime ÷ 0.32 s.
After flash: amber bar → any BLE central for `kConfirmDwellMs` (10 s) → `IMAGE_OK`.

## Build the DFU zip

Requires submodules (NimBLE + FreeRTOS are **ON** by default for sealed builds):

```powershell
git submodule update --init --recursive
cmake -S . -B build/dfu -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DBOOTLOADER_PRESENT=ON
cmake --build build/dfu
cmake --build build/dfu --target slate_dfu
```

Or:

```powershell
python scripts/package_dfu.py --bin build/dfu/slate_firmware.bin --version 0.1.0
```

Outputs: `slate-mcuboot-image.bin` + `slate-dfu.zip` (needs `imgtool` and ideally `adafruit-nrfutil`).

The image uses **`imgtool create`** with `--slot-size 475136` (InfiniTime contract), not Slate ECDSA signing.

Radio-less bring-up only: add `-DSLATE_HAS_NIMBLE=OFF` (companion cannot connect; prefer radio-on DFU for sealed watches).

## Install with the Slate companion

In the Slate app → **Install firmware (sealed)**:

1. In InfiniTime Settings, enable firmware updates. Charge to ≥30% (prefer charging).
2. Tap **Select InfiniTime / recovery (CDM)**. This is a separate association
   filter from normal Slate pairing and also works on API 30 without location
   permission because Android performs the scan through CDM.
   Confirming the CDM dialog can drop you back on the app's main screen — some
   builds tear the caller down. Just re-open **Install firmware (sealed)**; both
   the watch and the zip are remembered, and the status line says what is
   selected.
3. Select `slate-dfu.zip`, then tap **Validate and install**.
4. Keep the watch nearby. A `connectedDevice` foreground service owns the raw
   `BluetoothGatt` legacy-DFU transfer and reports packet progress.

The app accepts only an application image named `slate-mcuboot-image.bin` with
PineTime device type `0x0052` and an MCUBoot header. It rejects bootloader /
SoftDevice payloads and PineDFU targets. Target classification is still a
**heuristic** because MCUBoot itself has no BLE; a custom bootloader with a
different secondary map remains a residual risk.

## Flash (same tools as InfiniTime)

1. If the watch is already on a **radio-less** Slate build: hold button on the pinecone splash — **blue** = revert to InfiniTime, **red** = recovery DFU. Then continue from InfiniTime.
2. Watch on **InfiniTime** (or red-button **recovery**), battery ≥30%, preferably charging.
3. Phone: use the Slate companion flow above. **Gadgetbridge**, **Amazfish**, or
   **nRF Connect** remain fallback legacy-DFU clients.
4. Send `slate-dfu.zip` to the watch.
5. Watch reboots → InfiniTime MCUBoot swap → Slate must start within seconds (**WDT** via `pet_service` from the **app** task loop — not tick/idle).
6. Slate boots to the local face with an **amber bar across the top**. That bar
   means MCUBoot swapped this image in *on trial*: it is still unconfirmed and
   the next reset — including a flat battery — reverts to InfiniTime.
7. **Stop your DFU tool from holding the link.** Slate accepts **one** connection at
   a time (`SLATE_BLE_MAX_CONNECTIONS 1`). Gadgetbridge and Amazfish auto-reconnect
   to a known PineTime, so if either is still paired it will occupy the link and
   nothing else can connect. Disable auto-reconnect, or remove the device, before
   the next step. The companion **Troubleshooting** screen and sealed-DFU / reconnect /
   OTA / confirm flows detect this (GATT occupancy + bonded-but-silent) and show
   app-specific remediation instead of timing out silently.
8. Connect the **Slate companion** (preferred) — or nRF Connect / Gadgetbridge —
   and leave it connected for **10 seconds**. In the companion, a prominent banner
   shows **“image on trial — keep connected, N s to confirm”**, then switches to
   **“confirmed — image is permanent”** when `IMAGE_OK` is written (CONTROL
   `CONFIRM_STATUS` / 0xE1). The amber bar on the watch disappears at the same
   moment. There is no time limit on reaching this point, as long as the watch
   is not reset. If HELLO succeeds but the trial state never clears, the
   companion warns (often another app stole the single GATT slot — see step 7).
9. Only then set up the companion properly: associate, HELLO completes, companion
   pushes DISPLAY (clock / Timer). Tap on watch → INPUT on phone. Disconnect →
   local face (`on_link_down`).

Rollback while the bar is still amber: hold the side button ~7–8 s, or just let
the battery run down. Once confirmed, rollback needs the pinecone **blue** /
**red** button path from the InfiniTime bootloader.

### Why confirming needs a connection and not a timer

Confirm is the point of no return: after it, MCUBoot will not revert. The only
way back into a sealed watch is a DFU push over BLE, so the one thing worth
proving before confirming is that a phone can actually reach the GATT server. An
uptime timer or a button press would confirm an image whose radio never came up
and leave no way in without opening the case. Any central satisfies it, so you do
not have to finish Android permissions and CDM pairing first — see
`kConfirmDwellMs` in `include/boot_util.hpp`.

## After Slate is installed

**Two fully implemented post-install OTA paths are available:**

### 1. Channel-5 OTA (preferred — same `slate-dfu.zip`)
In the Slate companion → **Update Slate firmware (SDP OTA)**. Requires the watch
to already be connected to the companion. Select the same `slate-dfu.zip`, tap
Start. The MCUBoot image is hashed, streamed over SDP channel 5 with credit
flow-control, verified at commit, and confirmed via `IMAGE_OK` on the next reconnect.
Resumable across disconnects using the same transfer id. See `docs/ota.md §Channel 5 OTA`.

### 2. Nordic legacy DFU (fallback — Gadgetbridge / nRF Connect compatible)
The Slate firmware exposes `0x1530` DFU service using the InfiniTime/Nordic SDK 7.x
wire protocol (Start→Packet size→Init→PRN→ReceiveFW→data→Validate CRC-16→Activate).
The DFU Revision characteristic (`0x1534`) returns `0x0008`. Use Gadgetbridge,
Amazfish, nRF Connect, or the companion sealed installer with any `slate-dfu.zip`
built for this map. Disconnect at any stage resets transfer state cleanly.

## Do not

- OTA a replacement bootloader / reloader unless you accept brick risk with no SWD recovery.
- Flash a SoftDevice / wasp zip while expecting this MCUBoot layout.
- Expect a radio-less (`SLATE_HAS_NIMBLE=OFF`) image to confirm IMAGE_OK via the companion.
- Ship a `SLATE_BISECT_STAGE` image — debug-only; stages auto-return via WDT.

## Boot bisect DFUs (sealed crash <1 s)

If Slate paints then dies before durable IMAGE_OK (face under ~1 s → not WDT), build
stage images with diagnostics. **Charge the watch fully** first (exclude brownout).

Each stage shows a **pass colour for ~15 s** (motor silent), then goes black and
**stops petting the WDT** so InfiniTime returns. No forever hang — do not wait for
battery drain. Stages do **not** write IMAGE_OK (confirm would trap a radio-less
image on the watch).

| Stage | CMake | Pass criteria |
|---|---|---|
| 1 | `-DSLATE_BISECT_STAGE=1` | Solid **yellow** ~15 s, quiet motor → black → InfiniTime |
| 2 | `-DSLATE_BISECT_STAGE=2` | Solid **green** ~15 s, quiet motor → InfiniTime |
| 3 | `-DSLATE_BISECT_STAGE=3` | Solid **white** (advertising) then hold → InfiniTime |

```powershell
cmake -S . -B build/bisect1 -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release -DSLATE_BISECT_STAGE=1
cmake --build build/bisect1 --target slate_dfu
# zip: build/bisect1/slate-dfu.zip — flash; yellow 15s then InfiniTime = pass
```

Screen colours: red=main, orange=display, yellow=stage1 pass, green=stage2,
blue=NimBLE sync, white=stage3 advertising/pass, magenta=HardFault (+ hex),
cyan=`configASSERT` / malloc-fail / stack-overflow. Four squares = RESETREAS
(pin / WDT / soft / lockup). Fail colours hold ~15 s then starve WDT. Hold the
side button ~7–8 s anytime to force reset (software — same idea as InfiniTime).
Old forever-pet images: unplug and wait for battery drain.

**Hardware rules (also apply to production):** TIMER0 is reserved for NimBLE LL —
`board::micros()` uses **TIMER1**. MSP is **4 KB** with NimBLE. Bisect flags are
debug-only; production confirms **only** on BLE session (never a boot timer).

`confirm_image()` must write IMAGE_OK with a **32-bit** NVMC store (byte `STRB`
HardFaults on nRF52832). The haptic on P0.16 is **active-low** (same as InfiniTime:
drive high = off, drive low = on). Boot/bisect force the pin high immediately and
never pulse it — the earlier “drive low to silence” path was inverted and held the
motor on.
