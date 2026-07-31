// Host fakes for compiling src/wdt.cpp under SLATE_HOST_MMIO.
#include "board.hpp"
#include "nrf52832_regs.hpp"

#include <cstdint>

namespace {

bool g_button_pressed = false;
std::uint32_t g_wdt_rr0 = 0u;
std::uint32_t g_wdt_runstatus = 0u;

constexpr std::uintptr_t kWdtBase = 0x40010000u;
constexpr std::uintptr_t kRr0 = kWdtBase + 0x600u;
constexpr std::uintptr_t kRunstatus = kWdtBase + 0x400u;

}  // namespace

namespace slate {
namespace wdt {
namespace host_test {

void set_button_pressed(bool pressed) { g_button_pressed = pressed; }

std::uint32_t rr0() { return g_wdt_rr0; }

void set_rr0(std::uint32_t v) { g_wdt_rr0 = v; }

void set_runstatus(std::uint32_t v) { g_wdt_runstatus = v; }

}  // namespace host_test
}  // namespace wdt
}  // namespace slate

namespace nrf {
namespace host_mmio {

volatile std::uint32_t& cell(std::uintptr_t address) {
  if (address == kRr0) {
    return reinterpret_cast<volatile std::uint32_t&>(g_wdt_rr0);
  }
  if (address == kRunstatus) {
    return reinterpret_cast<volatile std::uint32_t&>(g_wdt_runstatus);
  }
  static std::uint32_t sink = 0u;
  sink = 0u;
  return reinterpret_cast<volatile std::uint32_t&>(sink);
}

}  // namespace host_mmio
}  // namespace nrf

namespace board {

bool button_raw() { return g_button_pressed; }

void poll_reboot_button() {}

void motor_off() {}

void busy_wait_us(std::uint32_t) {}

void busy_wait_ms(std::uint32_t) {}

void motor_on() {}

void pulse_motor(std::uint32_t) {}

std::uint32_t micros() { return 0u; }

[[noreturn]] void system_reset() {
  for (;;) {
  }
}

std::uint32_t reset_reason() { return 0u; }

void capture_reset_reason() {}

void button_hw_init() {}

}  // namespace board
