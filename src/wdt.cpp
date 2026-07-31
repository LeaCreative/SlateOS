#include "wdt.hpp"

#include "board.hpp"
#include "nrf52832_regs.hpp"

namespace slate {
namespace wdt {
namespace {

constexpr std::uintptr_t WDT_BASE = 0x40010000u;
constexpr std::uintptr_t RUNSTATUS = WDT_BASE + 0x400u;
constexpr std::uintptr_t RR0 = WDT_BASE + 0x600u;
// Magic reload value from nRF52 Product Spec.
constexpr std::uint32_t kRrReload = 0x6E524635u;

}  // namespace

bool is_running() {
  return (nrf::reg<std::uint32_t>(RUNSTATUS) & 1u) != 0u;
}

void pet() {
  // InfiniTime SystemTask: Reload only while the side button is released.
  // Holding the button starves the bootloader WDT (~7 s) even if tasks are
  // wedged — including when this is called from the tick ISR.
  if (board::button_raw()) {
    return;
  }
  nrf::reg<std::uint32_t>(RR0) = kRrReload;
}

}  // namespace wdt
}  // namespace slate

// Called from configPOST_SLEEP_PROCESSING in FreeRTOSConfig.h, which is included
// by C translation units, so this needs C linkage.
extern "C" void slate_wdt_pet(void) { slate::wdt::pet(); }

namespace slate {
namespace wdt {

void pet_service() {
  board::poll_reboot_button();
  pet();
}

[[noreturn]] void fatal_starve() {
#if !defined(SLATE_HOST_MMIO)
  __asm volatile("cpsid i" ::: "memory");
#endif
  board::motor_off();
  for (;;) {
    board::motor_off();
    board::poll_reboot_button();
    board::busy_wait_us(20000u);
  }
}

}  // namespace wdt
}  // namespace slate
