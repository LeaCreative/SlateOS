#pragma once

#include <cstdint>

// nRF52832 RTC1 tick source for wall_clock (LFCLK already running).
// With SLATE_FREERTOS_RTC_TICK, FreeRTOS owns the peripheral at 1024 Hz
// (PRESCALER=31); wall_clock shares the same COUNTER. Without FreeRTOS,
// rtc_hw starts RTC1 itself at 32768 Hz.

namespace slate {
namespace rtc_hw {

void init();

// 24-bit COUNTER + software overflow → 64-bit tick.
std::uint64_t ticks();

// Call from RTC1_IRQHandler on OVRFLW (port_rtc_tick or bare-metal path).
void on_overflow_irq();

}  // namespace rtc_hw
}  // namespace slate
