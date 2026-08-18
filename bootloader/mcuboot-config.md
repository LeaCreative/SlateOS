# MCUBoot configuration (Slate)

MCUBoot is **not** built inside this repository. Sealed PineTimes ship
[pinetime-mcuboot-bootloader](https://github.com/InfiniTimeOrg/pinetime-mcuboot-bootloader)
(InfiniTime). Slate **inherits that flash/image contract** so Gadgetbridge /
nRF Connect DFU works the same as InfiniTime updates. See `docs/flash-sealed.md`.

Build a PineTime-capable MCUBoot against this map only for SWD factory / custom BL.

## Required options (InfiniTime-compatible)

| Setting | Value |
|---|---|
| Primary slot | internal `0x8000`, size `0x74000` (475136) |
| Secondary slot | external SPI `0x40000`, size `0x74000` |
| Scratch | internal `0x7C000`, size `0x1000` |
| Recovery / assets | external SPI `0x0`, size `0x40000` |
| Signature | InfiniTime BL accepts `imgtool create` (unsigned). ECDSA path is for custom BL only |
| Swap | swap-using-scratch |
| Validate primary | on |
| Revert | if `IMAGE_OK` not set within **3** boots |
| Image header | 32 bytes (`--pad-header`) |
| Image version | `0.1.N` from `kVersion` `0.1.0-mN`. Keep in lockstep with the face stamp. InfiniTime README says “newer version”; this watch has already swapped equal `0.1.0` after `IMAGE_OK`. |

## Factory flash order (devkit / opened housing)

1. MCUBoot at `0x00000000`
2. Optional recovery image into external `0x0`
3. App image (`imgtool create`, slot 475136) into primary `0x8000` (or DFU after boot)
4. Enable APPROTECT
5. Power-cycle; never leave SWD attached for Gate C power measurements

## Button recovery (bootloader UI)

If the bootloader supports InfiniTime-style long-press:

- Short: normal boot
- Medium (blue): force revert
- Long (red): load recovery image from external flash and run DFU-capable recovery

Document the exact timing for the binary you ship; Slate app does not implement
the bootloader UI.
