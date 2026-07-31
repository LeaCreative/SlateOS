#include "rtc_hw.hpp"

#include "nrf52832_regs.hpp"

namespace slate {
namespace rtc_hw {
namespace {

constexpr std::uintptr_t RTC1_BASE = 0x40011000u;
constexpr std::uintptr_t TASKS_START = RTC1_BASE + 0x000u;
constexpr std::uintptr_t TASKS_STOP = RTC1_BASE + 0x004u;
constexpr std::uintptr_t TASKS_CLEAR = RTC1_BASE + 0x008u;
constexpr std::uintptr_t EVENTS_OVRFLW = RTC1_BASE + 0x10Cu;
constexpr std::uintptr_t INTENSET = RTC1_BASE + 0x304u;
constexpr std::uintptr_t EVTENSET = RTC1_BASE + 0x344u;
constexpr std::uintptr_t COUNTER = RTC1_BASE + 0x504u;
constexpr std::uintptr_t PRESCALER = RTC1_BASE + 0x508u;

constexpr std::uint32_t INT_OVRFLW = 1u << 1;
constexpr std::uint32_t irq_RTC1 = 17u;

volatile std::uint32_t g_overflows = 0u;

}  // namespace

void init() {
  g_overflows = 0u;
#if defined(SLATE_FREERTOS_RTC_TICK) && (SLATE_FREERTOS_RTC_TICK == 1)
  // FreeRTOS owns RTC1 (PRESCALER=31 → 1024 Hz COUNTER, TICK + OVRFLW IRQs).
  // vPortSetupTimerInterrupt starts the peripheral at scheduler start.
  return;
#else
  nrf::reg<std::uint32_t>(TASKS_STOP) = 1u;
  nrf::reg<std::uint32_t>(TASKS_CLEAR) = 1u;
  nrf::reg<std::uint32_t>(PRESCALER) = 0u;  // 32768 Hz (bare-metal / no RTOS)
  nrf::reg<std::uint32_t>(EVENTS_OVRFLW) = 0u;
  nrf::reg<std::uint32_t>(EVTENSET) = INT_OVRFLW;
  nrf::reg<std::uint32_t>(INTENSET) = INT_OVRFLW;
  nrf::nvic_clear_pending(irq_RTC1);
  nrf::nvic_enable_irq(irq_RTC1);
  nrf::reg<std::uint32_t>(TASKS_START) = 1u;
#endif
}

std::uint64_t ticks() {
  std::uint32_t ov;
  std::uint32_t ctr;
  do {
    ov = g_overflows;
    ctr = nrf::reg<std::uint32_t>(COUNTER) & 0x00FFFFFFu;
  } while (ov != g_overflows);
  return (static_cast<std::uint64_t>(ov) << 24) | ctr;
}

void on_overflow_irq() {
#if defined(SLATE_FREERTOS_RTC_TICK) && (SLATE_FREERTOS_RTC_TICK == 1)
  // Event already cleared in RTC1_IRQHandler (port_rtc_tick.c).
  ++g_overflows;
#else
  if (nrf::reg<std::uint32_t>(EVENTS_OVRFLW) != 0u) {
    nrf::reg<std::uint32_t>(EVENTS_OVRFLW) = 0u;
    ++g_overflows;
  }
#endif
}

}  // namespace rtc_hw
}  // namespace slate

extern "C" void slate_rtc1_on_overflow(void) {
  slate::rtc_hw::on_overflow_irq();
}

#if !defined(SLATE_FREERTOS_RTC_TICK) || (SLATE_FREERTOS_RTC_TICK == 0)
extern "C" void RTC1_IRQHandler() {
  slate::rtc_hw::on_overflow_irq();
}
#endif
