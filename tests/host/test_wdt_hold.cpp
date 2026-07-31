// Regression fence: WDT RR0 must not reload while the side button is held.
// Compiles real src/wdt.cpp against host MMIO + fake board::button_raw().

#include "wdt.hpp"

#include <cstdio>
#include <cstdint>

namespace slate {
namespace wdt {
namespace host_test {
void set_button_pressed(bool pressed);
std::uint32_t rr0();
void set_rr0(std::uint32_t v);
void set_runstatus(std::uint32_t v);
}  // namespace host_test
}  // namespace wdt
}  // namespace slate

extern "C" void slate_wdt_pet(void);

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_fails;
  }
}

constexpr std::uint32_t kReload = 0x6E524635u;
constexpr std::uint32_t kSentinel = 0xA5A5A5A5u;

void reset_regs() {
  slate::wdt::host_test::set_rr0(kSentinel);
  slate::wdt::host_test::set_runstatus(1u);
}

}  // namespace

int main() {
  using slate::wdt::host_test::set_button_pressed;
  using slate::wdt::host_test::rr0;

  // Released: pet() must write the Nordic reload magic.
  reset_regs();
  set_button_pressed(false);
  slate::wdt::pet();
  expect(rr0() == kReload, "pet() reloads RR0 when button released");

  // Held: pet() must leave RR0 untouched (InfiniTime SystemTask pattern).
  reset_regs();
  set_button_pressed(true);
  slate::wdt::pet();
  expect(rr0() == kSentinel, "pet() must not reload RR0 while button held");

  // pet_service() funnels through pet() — still withhold when held.
  reset_regs();
  set_button_pressed(true);
  slate::wdt::pet_service();
  expect(rr0() == kSentinel,
         "pet_service() must not reload RR0 while button held");

  reset_regs();
  set_button_pressed(false);
  slate::wdt::pet_service();
  expect(rr0() == kReload, "pet_service() reloads RR0 when button released");

  // configPOST_SLEEP_PROCESSING → slate_wdt_pet() → pet().
  reset_regs();
  set_button_pressed(true);
  slate_wdt_pet();
  expect(rr0() == kSentinel,
         "slate_wdt_pet() must not reload RR0 while button held");

  reset_regs();
  set_button_pressed(false);
  slate_wdt_pet();
  expect(rr0() == kReload, "slate_wdt_pet() reloads RR0 when button released");

  expect(slate::wdt::is_running(), "is_running() reads host RUNSTATUS");

  if (g_fails == 0) {
    std::printf("OK: wdt_hold\n");
    return 0;
  }
  std::printf("%d failure(s)\n", g_fails);
  return 1;
}
