#include "boot_diag.hpp"

#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "st7789.hpp"
#include "wdt.hpp"

#include <cstdint>

namespace boot_diag {

void fill(std::uint16_t rgb565);  // used by fail-path WDT release below

namespace {

// Tiny 3x5 digits (rows top→bottom). Bit2 = leftmost pixel (MSB-left) so glyphs
// match ST7789 MADCTL=0x00 column order. The previous LSB-left font appeared
// mirrored on PineTime and made sealed-watch assert hex unreadable.
constexpr std::uint8_t kFont3x5[16][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},  // 0
    {0x2, 0x2, 0x2, 0x2, 0x2},  // 1
    {0x7, 0x4, 0x7, 0x1, 0x7},  // 2
    {0x7, 0x4, 0x7, 0x4, 0x7},  // 3
    {0x5, 0x5, 0x7, 0x4, 0x4},  // 4
    {0x7, 0x1, 0x7, 0x4, 0x7},  // 5
    {0x7, 0x1, 0x7, 0x5, 0x7},  // 6
    {0x7, 0x4, 0x4, 0x4, 0x4},  // 7
    {0x7, 0x5, 0x7, 0x5, 0x7},  // 8
    {0x7, 0x5, 0x7, 0x4, 0x7},  // 9
    {0x7, 0x5, 0x7, 0x5, 0x5},  // A
    {0x1, 0x1, 0x7, 0x5, 0x7},  // B
    {0x7, 0x1, 0x1, 0x1, 0x7},  // C
    {0x4, 0x4, 0x7, 0x5, 0x7},  // D
    {0x7, 0x1, 0x7, 0x1, 0x7},  // E
    {0x7, 0x1, 0x7, 0x1, 0x1},  // F
};

void put_be16(std::uint8_t* dst, std::uint16_t c) {
  dst[0] = static_cast<std::uint8_t>((c >> 8) & 0xFFu);
  dst[1] = static_cast<std::uint8_t>(c & 0xFFu);
}

// Static — never put a 480B line on a NimBLE host stack (~2 KB).
alignas(4) std::uint8_t g_line[240 * 2];

void fill_rect(std::uint16_t x0, std::uint16_t y0, std::uint16_t x1,
               std::uint16_t y1, std::uint16_t rgb565) {
  if (x1 < x0 || y1 < y0) {
    return;
  }
  st7789::set_window(x0, y0, x1, y1);
  const std::uint16_t w = static_cast<std::uint16_t>(x1 - x0 + 1u);
  for (std::uint16_t x = 0; x < w; ++x) {
    put_be16(&g_line[x * 2u], rgb565);
  }
  const std::uint32_t row_bytes = static_cast<std::uint32_t>(w) * 2u;
  std::uint16_t rows_since_pet = 0;
  for (std::uint16_t y = y0; y <= y1; ++y) {
    // Re-arm RAMWR each row: CS was dropped after the previous transfer.
    st7789::set_window(x0, y, x1, y);
    (void)st7789::write_pixels(g_line, row_bytes);
    if (++rows_since_pet >= 32u) {
      slate::wdt::pet();
      board::motor_off();
      rows_since_pet = 0;
    }
  }
}

void draw_hex_nibble(std::uint16_t x, std::uint16_t y, std::uint8_t n,
                     std::uint16_t fg, std::uint16_t bg) {
  n &= 0xFu;
  constexpr std::uint16_t scale = 3u;
  for (std::uint16_t row = 0; row < 5u; ++row) {
    const std::uint8_t bits = kFont3x5[n][row];
    for (std::uint16_t col = 0; col < 3u; ++col) {
      const std::uint16_t color = (bits & (1u << col)) ? fg : bg;
      const std::uint16_t px = static_cast<std::uint16_t>(x + col * scale);
      const std::uint16_t py = static_cast<std::uint16_t>(y + row * scale);
      fill_rect(px, py, static_cast<std::uint16_t>(px + scale - 1u),
                static_cast<std::uint16_t>(py + scale - 1u), color);
    }
  }
}

void draw_hex32(std::uint16_t x, std::uint16_t y, std::uint32_t v,
                std::uint16_t fg, std::uint16_t bg) {
  for (int i = 7; i >= 0; --i) {
    const std::uint8_t nib = static_cast<std::uint8_t>((v >> (i * 4)) & 0xFu);
    draw_hex_nibble(x, y, nib, fg, bg);
    x = static_cast<std::uint16_t>(x + 12u);
  }
}

// Sealed watches cannot attach a debugger: never pet forever. Hold the
// diagnostic colour briefly (IRQs off so the idle hook cannot pet), then
// starve the InfiniTime BL WDT so MCUBoot reverts the unconfirmed image.
// Side button held ~7–8 s → InfiniTime-style WDT reset / SYSRESETREQ.
[[noreturn]] void hold_then_wdt_release(std::uint32_t hold_ms) {
  __asm volatile("cpsid i" ::: "memory");
  std::uint32_t left = hold_ms;
  while (left > 0u) {
    board::motor_off();
    slate::wdt::pet_service();
    const std::uint32_t chunk = (left > 200u) ? 200u : left;
    board::busy_wait_ms(chunk);
    left -= chunk;
  }
  fill(kBlack);
  slate::wdt::fatal_starve();
}

}  // namespace

void fill(std::uint16_t rgb565) {
  fill_rect(0, 0, static_cast<std::uint16_t>(st7789::kWidth - 1u),
            static_cast<std::uint16_t>(st7789::kHeight - 1u), rgb565);
}

void paint_resetreas() {
  const std::uint32_t rr = nrf::reg<std::uint32_t>(nrf::power::RESETREAS);
  fill(kBlack);
  const std::uint16_t colors[4] = {kRed, kOrange, kYellow, kCyan};
  // RESETREAS: RESETPIN, DOG, SREQ, LOCKUP
  const std::uint32_t bits[4] = {1u << 0, 1u << 1, 1u << 2, 1u << 3};
  for (std::uint16_t i = 0; i < 4u; ++i) {
    const std::uint16_t x0 = static_cast<std::uint16_t>(20u + i * 55u);
    const std::uint16_t on = (rr & bits[i]) != 0u ? colors[i] : 0x2104u;
    fill_rect(x0, 90, static_cast<std::uint16_t>(x0 + 50u), 150, on);
  }
  // Clear so the next boot sees a fresh reason.
  nrf::reg<std::uint32_t>(nrf::power::RESETREAS) = 0xFFFFFFFFu;
  // Brief so sealed bisect does not burn time before the pass colour.
  board::busy_wait_ms(200u);
}

void hardfault_paint_and_hang_from_frame(const std::uint32_t* stacked) {
  board::motor_off();
  constexpr std::uintptr_t kCfsr = 0xE000ED28u;
  constexpr std::uintptr_t kHfsr = 0xE000ED2Cu;
  const std::uint32_t cfsr = nrf::reg<std::uint32_t>(kCfsr);
  const std::uint32_t hfsr = nrf::reg<std::uint32_t>(kHfsr);
  const std::uint32_t pc = (stacked != nullptr) ? stacked[6] : 0u;
  fill(kMagenta);
  draw_hex32(8, 40, cfsr, kWhite, kMagenta);
  draw_hex32(8, 70, hfsr, kWhite, kMagenta);
  draw_hex32(8, 100, pc, kWhite, kMagenta);
  hold_then_wdt_release(15000u);
}

void hardfault_paint_and_hang() {
  hardfault_paint_and_hang_from_frame(nullptr);
}

void assert_paint_and_hang() {
  fill(kCyan);
  hold_then_wdt_release(15000u);
}

std::uint32_t file_hash(const char* path) {
  if (path == nullptr) {
    return 0u;
  }
  // Basename only: __FILE__ may be absolute or relative depending on the build.
  const char* base = path;
  for (const char* p = path; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') {
      base = p + 1;
    }
  }
  std::uint32_t h = 2166136261u;
  for (const char* p = base; *p != '\0'; ++p) {
    h ^= static_cast<std::uint8_t>(*p);
    h *= 16777619u;
  }
  return h;
}

void assert_paint_and_hang_at(const char* file, int line) {
  board::motor_off();
  fill(kCyan);
  draw_hex32(8, 60, file_hash(file), kBlack, kCyan);
  draw_hex32(8, 110, static_cast<std::uint32_t>(line), kBlack, kCyan);
  hold_then_wdt_release(15000u);
}

void fatal_paint_and_hang(std::uint16_t rgb565) {
  board::motor_off();
  fill(rgb565);
  hold_then_wdt_release(15000u);
}

}  // namespace boot_diag

// Strong definition overrides the weak alias in startup.cpp. Present in every
// build: a sealed watch must show why it died, not just vanish back to InfiniTime.
extern "C" void slate_hardfault_c(std::uint32_t* stacked) {
  boot_diag::hardfault_paint_and_hang_from_frame(stacked);
}

extern "C" __attribute__((naked)) void HardFault_Handler() {
  __asm volatile(
      " tst lr, #4                        \n"
      " ite eq                            \n"
      " mrseq r0, msp                     \n"
      " mrsne r0, psp                     \n"
      " b slate_hardfault_c               \n");
}
