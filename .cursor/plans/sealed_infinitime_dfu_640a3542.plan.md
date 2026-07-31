---
name: Sealed InfiniTime DFU
overview: Enable sealed-watch install via InfiniTime’s existing DFU+MCUBoot contract (first hop + swap layout), while keeping Slate’s own OTA (SDP channel 5 + companion) as the preferred ongoing update path once Slate is resident. Align flash map to InfiniTime so both paths write a secondary slot MCUBoot already understands.
todos:
  - id: align-flash-map
    content: Align flash_map.hpp + linker + docs to InfiniTime primary 475136 / secondary SPI 0x40000; move LittleFS to 0xB4000
    status: completed
  - id: dfu-ota-writers
    content: Point ota_slot + Nordic DFU at InfiniTime secondary; write InfiniTime pending-image magic
    status: completed
  - id: wdt-pet
    content: Add NRF WDT petting so InfiniTime bootloader does not revert after swap
    status: completed
  - id: package-dfu
    content: scripts/package_dfu.py + CMake slate_dfu → imgtool create + adafruit-nrfutil zip
    status: completed
  - id: docs-sealed
    content: "docs/flash-sealed.md: Gadgetbridge/nRF Connect InfiniTime→Slate procedure + confirm/rollback"
    status: completed
  - id: preflight-probe
    content: Companion preflight GATT probe (InfiniTime/DFU vs PineDFU) before offering slate-dfu.zip; block SoftDevice path
    status: completed
isProject: false
---

# Sealed-watch DFU via InfiniTime MCUBoot

## Why SWD docs aren’t enough

A sealed PineTime keeps **pinetime-mcuboot-bootloader**. OTA works like this:

```mermaid
sequenceDiagram
  participant Phone
  participant InfiniTime as InfiniTime_or_recovery
  participant SPI as ExtSPI_0x40000
  participant BL as MCUBoot
  participant Slate as Slate_app

  Phone->>InfiniTime: DFU zip (legacy 0x1530)
  InfiniTime->>SPI: write image + magic
  InfiniTime->>BL: reset
  BL->>BL: swap secondary to primary
  BL->>Slate: jump 0x8000 + start WDT
  Slate->>Slate: pet WDT; BLE IMAGE_OK
```

You already did the phone side of this for InfiniTime. Slate must produce a **compatible zip** and **survive** after swap.

## Why not “just roll our own OTA” (suited to Slate)?

We **can and already should** own the *download and UX* side. What we cannot freely redesign on a sealed watch is the *boot* side.

| Layer | Who owns it on sealed PineTime | Slate-suited redesign? |
|---|---|---|
| **Transport + UI** (how bytes get to SPI) | Running **app** (InfiniTime first; then Slate) | **Yes.** Channel-5 OTA, companion progress, resume, battery gate, credit — this is Slate’s mechanism. Nordic legacy DFU is only the interoperable fallback. |
| **Image header / slot sizes / swap / revert** | **MCUBoot already in flash** | **No** (without replacing the bootloader). MCUBoot alone decides whether an image is bootable and how primary↔secondary swap works. |
| **Bootloader binary itself** | Factory / SWD / InfiniTime “reloader” (brick-prone) | **Not for field.** Roadmap + `docs/ota.md`: never OTA the BL on sealed units; no field recovery if BL is wrong. |

So:

1. **First install** (InfiniTime → Slate): must speak InfiniTime’s DFU writer + MCUBoot contract (zip, slot `475136`, secondary `@0x40000`, WDT, magic). That is not “Slate’s preferred OTA” — it is the *only* sealed on-ramp.
2. **Later updates** (Slate → Slate): use **Slate’s OTA** (SDP ch5 via companion). That *is* the Slate-suited mechanism. It still must **land the same MCUBoot image in the same secondary slot**, because the same InfiniTime MCUBoot will perform the swap. Aligning the flash map makes Slate OTA *and* legacy DFU both valid writers into one bootloader-defined hole.
3. **Custom Slate bootloader** (different map, ECDSA-only, no InfiniTime magic): possible only via SWD factory or a one-shot reloader OTA. Explicitly out of scope for sealed self-serve install; high brick risk, no SWD fallback.

Constraint in one line: **own the pipe; inherit the mailbox.** Slate designs the pipe; MCUBoot defines the mailbox.

### Ecosystem precedent

| Project | Sealed install pattern |
|---|---|
| **InfiniTime → InfiniTime** | Nordic DFU zip → secondary `@0x40000` → MCUBoot swap. App owns download; BL owns swap. |
| **Other MCUBoot-native apps** (intended use of pinetime-mcuboot) | Same DFU + MCUBoot contract; InfiniTime docs explicitly allow third-party firmwares this way. |
| **wasp-os** | **Different:** DFU a **reloader** that *replaces* MCUBoot with Adafruit/SoftDevice BL, then DFU MicroPython. Needed because wasp requires SoftDevice. Brick risk on power loss; reverse path needs another reloader. |
| **Slate** | Same class as InfiniTime (NimBLE, no SoftDevice) → **inherit MCUBoot**, don’t reloader-replace BL. Slate-suited OTA is the post-install pipe. |

### Preflight: can we “query InfiniTime bootloader”?

**No direct query.** InfiniTime MCUBoot has **no BLE stack** (by design). Bootloader version is only on the pinecone splash, not over the air.

**Yes to a strong heuristic** from the **running application** before offering DFU:

| Signal | Means |
|---|---|
| GATT Device Information / `0x2A26` firmware revision looks like InfiniTime (`x.y.z`) | InfiniTime app (or recovery) is up — almost always paired with InfiniTime MCUBoot on sealed units |
| Nordic legacy DFU present (`0x1530` / `1531` / `1532`) | App can accept a DFU zip into secondary |
| InfiniTime-ish advertising / known InfiniTime service UUIDs | Corroborate |
| Advertises **PineDFU** / SoftDevice-style DFU-only mode | **Refuse** Slate MCUBoot zip — wasp (or similar) BL path; needs reloader, not our image |
| Already Slate (Slate GATT / companion bond) | Use **channel-5 OTA**, not InfiniTime-first-hop DFU |

Companion UX: **Probe → Allow / Block** with plain language (“Looks like InfiniTime + MCUBoot DFU — safe to install Slate” vs “SoftDevice/PineDFU detected — do not flash this zip”). Document that this is **inference**, not a BL attestation; the residual risk is a rare non-InfiniTime MCUBoot with a divergent secondary map.

## Incompatibility today (must fix)

| Item | InfiniTime bootloader | Slate now |
|---|---|---|
| Primary slot | `0x8000`, **475136** (`0x74000`) | `0x8000`, **320 KiB** (`0x50000`) |
| Scratch | `0x7C000` | `0x58000` |
| Secondary (SPI) | **`0x40000`**, size 475136 ([InfiniTime `DfuService.h`](https://github.com/InfiniTimeOrg/InfiniTime/blob/develop/src/components/ble/DfuService.h)) | **`0x0`**, size `0x58000` |
| Image pack | `imgtool create … --slot-size 475136` (typically **unsigned**) | `imgtool sign` + Slate key + slot `0x50000` |
| Watchdog | BL starts WDT before app jump | **Slate does not pet WDT** → soft-brick / revert risk |

Until the map matches InfiniTime, a DFU zip either won’t swap cleanly or will brick the next OTA / LFS layout.

## Approach (chosen default)

**Inherit InfiniTime MCUBoot’s flash/image contract; keep Slate OTA as the primary ongoing updater.**

- Realign [`include/flash_map.hpp`](include/flash_map.hpp), linker, OTA/DFU writers to InfiniTime’s map so channel-5 and legacy DFU both target the bootloader’s secondary slot.
- Sealed **first hop**: InfiniTime-compatible **`imgtool create`** + `slate-dfu.zip` (unsigned, slot 475136) via Gadgetbridge/nRF Connect.
- Sealed **ongoing**: companion channel-5 OTA (already sketched in M15) writing the same secondary; legacy DFU remains recovery when SDP is broken.
- Slate ECDSA `imgtool sign` path stays for SWD/custom-BL factory builds only — not required for InfiniTime BL (which accepts `create` images).

### 1. Realign flash map + linker

Update [`include/flash_map.hpp`](include/flash_map.hpp), [`CMakeLists.txt`](CMakeLists.txt) (`SLATE_FLASH_LENGTH`), [`bootloader/mcuboot-config.md`](bootloader/mcuboot-config.md), [`docs/ota.md`](docs/ota.md):

**Internal:** primary `0x8000` / `0x74000`; scratch `0x7C000` / `0x1000`; keep persist at `0x7F000` (inside InfiniTime “spare”).

**External:** secondary **`0x40000` / `0x74000`**; recovery/assets `0x0`–`0x40000`; LittleFS starts at **`0xB4000`** (breaking LFS format — document wipe).

### 2. DFU / OTA writers must hit InfiniTime secondary

- [`src/ota_slot.cpp`](src/ota_slot.cpp) / channel-5 OTA: use new secondary constants.
- [`src/dfu_nordic.cpp`](src/dfu_nordic.cpp) (+ GATT path): write at `0x40000`, max **475136**, and on complete write InfiniTime’s **pending-image magic** at end of slot (same four words as InfiniTime `DfuImage::WriteMagicNumber`) so MCUBoot sees a pending upgrade the same way InfiniTime does.
- Host tests under `tests/host` that assume old sizes/offsets.

### 3. Pet the InfiniTime bootloader watchdog

Add a small WDT driver (NRF52832 `NRF_WDT`) and kick it from the main loop / idle path before the ~7s timeout. Without this, first boot after DFU often resets and **reverts to InfiniTime**.

Confirm path already exists ([`src/boot_util.cpp`](src/boot_util.cpp) / BLE `IMAGE_OK`); keep confirm-on-Slate-session so a dead BLE stack still reverts.

### 4. Package script + CMake artifact (the “how you flash”)

Extend / replace packaging:

- [`scripts/sign_firmware.py`](scripts/sign_firmware.py) → add **`scripts/package_dfu.py`** (or mode `--infinitime`):
  - `imgtool create --align 4 --header-size 32 --pad-header --slot-size 475136 --version …`
  - `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application … slate-dfu.zip`
- CMake custom target (e.g. `slate_dfu`) that builds Release/`BOOTLOADER_PRESENT=ON` and emits `build/.../slate-dfu.zip`.

### 5. User doc: sealed flash procedure

New [`docs/flash-sealed.md`](docs/flash-sealed.md) (link from [`docs/bringup.md`](docs/bringup.md) / [`docs/ota.md`](docs/ota.md)):

1. Watch on **InfiniTime** (or InfiniTime recovery), battery ≥30%, charging preferred.
2. Build: `cmake … -DBOOTLOADER_PRESENT=ON` → `ninja slate_dfu` (or script).
3. Phone: Gadgetbridge / Amazfish / nRF Connect → legacy DFU of `slate-dfu.zip` (same as InfiniTime update).
4. Watch reboots → MCUBoot swap → Slate must appear within seconds (WDT).
5. Connect Slate companion once to set **IMAGE_OK**; if it fails, wait for revert or hold button to recovery and DFU again.
6. Rollback: bootloader blue (revert) / red (recovery) per InfiniTime BL UI.

## Out of scope

- Building or OTA’ing a replacement MCUBoot (sealed brick risk).
- Changing companion Android install steps (unchanged; only needed after Slate is on the watch).
