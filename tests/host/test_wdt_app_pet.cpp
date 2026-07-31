// App-task-only WDT pets (InfiniTime SystemTask analogue).
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

// Stand-ins for FreeRTOS hooks after N-2: must never touch the WDT.
void fake_tick_hook_telemetry_only() {
  // Intentionally empty — production vApplicationTickHook does not call pet().
}
void fake_idle_hook() {
  // Intentionally empty — production vApplicationIdleHook does not call pet().
}

}  // namespace

int main() {
  using slate::wdt::host_test::set_button_pressed;
  using slate::wdt::host_test::rr0;

  // Simulated app_loop iterations: pet_service reloads when button up.
  reset_regs();
  set_button_pressed(false);
  for (int i = 0; i < 5; ++i) {
    slate::wdt::pet_service();
  }
  expect(rr0() == kReload, "app_loop pets reload RR0");

  // Wedge: no app iterations; tick/idle hooks must not reload.
  reset_regs();
  set_button_pressed(false);
  for (int i = 0; i < 100; ++i) {
    fake_tick_hook_telemetry_only();
    fake_idle_hook();
  }
  expect(rr0() == kSentinel,
         "wedged app: tick/idle hooks must not reload RR0");

  // Future tickless POST_SLEEP still may pet while app is in vTaskDelay —
  // that path is slate_wdt_pet, not the idle hook. Verify it still works when
  // called explicitly, and still withholds on button hold.
  reset_regs();
  set_button_pressed(false);
  slate_wdt_pet();
  expect(rr0() == kReload, "slate_wdt_pet reloads when button up");

  reset_regs();
  set_button_pressed(true);
  for (int i = 0; i < 5; ++i) {
    slate::wdt::pet_service();
  }
  expect(rr0() == kSentinel,
         "button held: app_loop pets must not reload RR0");

  if (g_fails == 0) {
    std::printf("OK: wdt_app_pet\n");
    return 0;
  }
  std::printf("%d failure(s)\n", g_fails);
  return 1;
}
