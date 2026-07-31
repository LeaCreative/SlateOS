#pragma once

#include <cstdint>

// Sealed-watch boot diagnostics — compiled into every build.
// A sealed watch has no SWD and no RTT, so a fatal fault is otherwise silent:
// the app stops petting, the InfiniTime BL WDT resets at ~7 s, and MCUBoot
// reverts an unconfirmed image. The screen is the only post-mortem channel.
//
// Production fatal colour code (held ~15 s, then WDT release → InfiniTime):
//   RED     FreeRTOS malloc failed / task create failed (heap exhausted)
//   ORANGE  FreeRTOS stack overflow
//   CYAN    configASSERT
//   MAGENTA HardFault (+ CFSR / HFSR / PC in hex)

namespace boot_diag {

constexpr std::uint16_t kRed     = 0xF800u;
constexpr std::uint16_t kOrange  = 0xFD20u;
constexpr std::uint16_t kYellow  = 0xFFE0u;
constexpr std::uint16_t kGreen   = 0x07E0u;
constexpr std::uint16_t kBlue    = 0x001Fu;
constexpr std::uint16_t kWhite   = 0xFFFFu;
constexpr std::uint16_t kMagenta = 0xF81Fu;
constexpr std::uint16_t kBlack   = 0x0000u;
constexpr std::uint16_t kCyan    = 0x07FFu;

/** Full-screen RGB565 fill (big-endian on wire). */
void fill(std::uint16_t rgb565);

/** Paint RESETREAS as four 60x60 squares then clear the register.
 *  Order L→R: pin reset, WDT, soft reset, lockup. Lit = bit set. */
void paint_resetreas();

/** Magenta screen + CFSR/HFSR/PC hex, hold ~15s, then starve WDT → InfiniTime. */
[[noreturn]] void hardfault_paint_and_hang();

/** Same as hardfault_paint_and_hang, using the exception stacked frame. */
[[noreturn]] void hardfault_paint_and_hang_from_frame(const std::uint32_t* stacked);

/** configASSERT / malloc / stack-overflow: cyan ~15s, then starve WDT → InfiniTime. */
[[noreturn]] void assert_paint_and_hang();

/** FNV-1a 32-bit of a source file's basename — decode with scripts/assert_hashes.py. */
std::uint32_t file_hash(const char* path);

/** Cyan + FNV-1a(basename) and line number in hex, then WDT release. */
[[noreturn]] void assert_paint_and_hang_at(const char* file, int line);

/** Solid diagnostic colour ~15s, then starve WDT → MCUBoot reverts. */
[[noreturn]] void fatal_paint_and_hang(std::uint16_t rgb565);

}  // namespace boot_diag
