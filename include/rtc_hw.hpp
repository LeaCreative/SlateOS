#pragma once

#include <cstdint>

// nRF52832 RTC1 as 32768 Hz tick source for wall_clock (LFCLK already running).

namespace slate {
namespace rtc_hw {

void init();

// 24-bit COUNTER + software overflow → 64-bit tick.
std::uint64_t ticks();

// Call from RTC1_IRQHandler (COMPARE0 overflow).
void on_overflow_irq();

}  // namespace rtc_hw
}  // namespace slate
