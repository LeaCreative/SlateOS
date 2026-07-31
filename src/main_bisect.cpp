// Sealed-watch boot bisect entry (SLATE_BISECT_STAGE 1|2|3).
// Timed colour hold, then release WDT so InfiniTime returns — no forever hang.
// Do not confirm IMAGE_OK here (confirm would stick the trial image with no BLE exit).
// Haptic is active-low: keep P0.16 driven HIGH (off).

#include "backlight.hpp"
#include "board.hpp"
#include "boot_diag.hpp"
#include "spi_bus.hpp"
#include "st7789.hpp"
#include "wdt.hpp"

#include <cstdint>

#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE >= 2)
#include "FreeRTOS.h"
#include "task.h"
#endif

#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE >= 3)
#include "ble_gatt.hpp"
#endif

namespace {

// How long the pass colour stays visible before WDT recovery.
constexpr std::uint32_t kPassHoldMs = 15000u;

void display_up() {
  spi::init();
  st7789::init();
  backlight::init();
  backlight::set(55u);
}

/** Show pass colour, pet WDT, then stop petting so InfiniTime BL reboots/reverts. */
[[noreturn]] void pass_hold_then_wdt_release(std::uint16_t color) {
  boot_diag::fill(color);
  std::uint32_t left = kPassHoldMs;
  while (left > 0u) {
    board::motor_off();
    slate::wdt::pet_service();
    const std::uint32_t chunk = (left > 200u) ? 200u : left;
    board::busy_wait_ms(chunk);
    left -= chunk;
  }
  // Black = “releasing WDT → InfiniTime”. Do not pet.
  boot_diag::fill(boot_diag::kBlack);
  slate::wdt::fatal_starve();
}

#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE == 2)
void bisect2_task(void*) {
  pass_hold_then_wdt_release(boot_diag::kGreen);
}
#endif

#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE == 3)
void bisect3_task(void*) {
  static ble::Link link;
  static ble::GattServer gatt;
  ble::GattCaps caps{};
  caps.battery = false;
  caps.device_info = false;
  gatt.init(&link, caps);
  ble::start_stack(&gatt, ble::SessionProfile::Active);

  // Wait until advertising (white painted from on_sync), then hold + release.
  for (int i = 0; i < 50; ++i) {
    board::motor_off();
    slate::wdt::pet_service();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  pass_hold_then_wdt_release(boot_diag::kWhite);
}
#endif

}  // namespace

#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE >= 2)
extern "C" void vApplicationIdleHook(void) {
  board::motor_off();
  slate::wdt::pet_service();
}

extern "C" void vApplicationMallocFailedHook(void) {
  boot_diag::assert_paint_and_hang();
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char*) {
  boot_diag::assert_paint_and_hang();
}

extern "C" void vAssertCalled(const char*, int) {
  boot_diag::assert_paint_and_hang();
}
#endif

extern "C" int main() {
  board::motor_off();
  board::button_hw_init();
  slate::wdt::pet();

  display_up();
  boot_diag::fill(boot_diag::kRed);
  boot_diag::paint_resetreas();
  boot_diag::fill(boot_diag::kOrange);

#if !defined(SLATE_BISECT_STAGE) || (SLATE_BISECT_STAGE == 1)
  // Stage 1: solid yellow ~15s (quiet motor) → black → WDT → InfiniTime.
  // IMAGE_OK confirm is deferred — confirming would trap a radio-less image.
  pass_hold_then_wdt_release(boot_diag::kYellow);
#elif SLATE_BISECT_STAGE == 2
  if (xTaskCreate(bisect2_task, "b2", configMINIMAL_STACK_SIZE + 128, nullptr,
                  tskIDLE_PRIORITY + 2, nullptr) != pdPASS) {
    boot_diag::assert_paint_and_hang();
  }
  vTaskStartScheduler();
  boot_diag::assert_paint_and_hang();
#elif SLATE_BISECT_STAGE == 3
  if (xTaskCreate(bisect3_task, "b3", 1024, nullptr, tskIDLE_PRIORITY + 2,
                  nullptr) != pdPASS) {
    boot_diag::assert_paint_and_hang();
  }
  vTaskStartScheduler();
  boot_diag::assert_paint_and_hang();
#else
#error "SLATE_BISECT_STAGE must be 1, 2, or 3"
#endif
  return 0;
}
