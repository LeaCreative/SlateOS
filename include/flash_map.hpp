#pragma once

#include <cstdint>

// Slate flash map — aligned to InfiniTime pinetime-mcuboot-bootloader so
// sealed watches can DFU Slate the same way as InfiniTime.
// See docs/ota.md and docs/flash-sealed.md.

namespace slate {
namespace flash_map {

// ── Internal flash (512 KiB) ─────────────────────────────────────────────────
constexpr std::uint32_t kInternalFlashSize = 0x00080000u;

// Bootloader 28 KiB + unused log 4 KiB = 32 KiB to app start.
constexpr std::uint32_t kMcuBootOffset = 0x00000000u;
constexpr std::uint32_t kMcuBootSize   = 0x00008000u;  // 32 KiB

// InfiniTime primary: 464 KiB (475136) including MCUBoot image trailer room.
// App code is linked at kPrimaryOffset + kImageHeaderSize (0x8020).
constexpr std::uint32_t kPrimaryOffset = 0x00008000u;
constexpr std::uint32_t kPrimarySize   = 0x00074000u;  // 475136

constexpr std::uint32_t kScratchOffset = 0x0007C000u;
constexpr std::uint32_t kScratchSize   = 0x00001000u;  // 4 KiB

// InfiniTime "spare" 12 KiB (0x7D000–0x80000); persist uses last page.
constexpr std::uint32_t kReservedOffset = 0x0007D000u;
constexpr std::uint32_t kReservedSize   = 0x00003000u;  // 12 KiB

constexpr std::uint32_t kPersistOffset = 0x0007F000u;
constexpr std::uint32_t kPersistSize   = 0x00001000u;  // 4 KiB

// MCUBoot image header pad (imgtool --header-size / --pad-header).
constexpr std::uint32_t kImageHeaderSize = 32u;

// Swap-with-revert: confirm within this many boots or MCUBoot reverts.
constexpr std::uint32_t kConfirmBootLimit = 3u;

// ── External SPI NOR (4 MiB) — InfiniTime layout ─────────────────────────────
constexpr std::uint32_t kExtFlashSize = 0x00400000u;

// Bootloader assets / recovery image (InfiniTime recovery lives here).
constexpr std::uint32_t kRecoveryOffset = 0x00000000u;
constexpr std::uint32_t kRecoverySize   = 0x00040000u;  // 256 KiB

// InfiniTime DfuService::writeOffset — OTA secondary slot.
constexpr std::uint32_t kSecondaryOffset = 0x00040000u;
constexpr std::uint32_t kSecondarySize   = 0x00074000u;  // 475136

// LittleFS after secondary (breaking change vs older Slate map at 0x80000).
constexpr std::uint32_t kLfsOffset = 0x000B4000u;
constexpr std::uint32_t kLfsSize   = 0x0034C000u;  // ~3.3 MiB

// InfiniTime pending-image magic (written at end of secondary after DFU).
// Same four words as InfiniTime DfuImage::WriteMagicNumber.
constexpr std::uint32_t kDfuMagic0 = 0xf395c277u;
constexpr std::uint32_t kDfuMagic1 = 0x7fefd260u;
constexpr std::uint32_t kDfuMagic2 = 0x0f505235u;
constexpr std::uint32_t kDfuMagic3 = 0x8079b62cu;

// OTA refuse threshold (percent); charging bypasses the gate.
constexpr std::uint8_t kOtaMinBatteryPercent = 30u;

}  // namespace flash_map
}  // namespace slate
