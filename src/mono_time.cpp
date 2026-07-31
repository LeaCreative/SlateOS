#include "mono_time.hpp"

#include "rtc_hw.hpp"

namespace slate {
namespace time {

std::uint32_t mono_ms() {
  // Same rates as wall_clock.cpp / rtc_hw ownership.
#if defined(SLATE_FREERTOS_RTC_TICK) && (SLATE_FREERTOS_RTC_TICK == 1)
  constexpr std::uint64_t kTicksPerSec = 1024u;
#else
  constexpr std::uint64_t kTicksPerSec = 32768u;
#endif
  // rtc_hw::ticks() is overflow-extended u64 (24-bit COUNTER + soft OVRFLW).
  // Truncating ms to u32 wraps at 2^32 — safe for uint32 elapsed math.
  const std::uint64_t ticks = rtc_hw::ticks();
  return static_cast<std::uint32_t>((ticks * 1000u) / kTicksPerSec);
}

}  // namespace time
}  // namespace slate
