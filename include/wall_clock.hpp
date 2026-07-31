#pragma once

#include <cstdint>

// Wall clock: epoch seconds + ppm drift vs an injected free-running tick source.
// On device the tick source is RTC1 (LFCLK 32.768 kHz). Survives reboot via persist.

namespace slate {
namespace clock {

struct Civil {
  std::uint16_t year;   // full year
  std::uint8_t month;   // 1-12
  std::uint8_t day;     // 1-31
  std::uint8_t hour;    // 0-23
  std::uint8_t minute;  // 0-59
  std::uint8_t second;  // 0-59
  std::uint8_t wday;    // 0=Sun … 6=Sat
};

struct Hooks {
  // Monotonic tick count (RTC1 COUNTER + overflow<<24). Rate is
  // kTicksPerSec in wall_clock.cpp — 1024 Hz with FreeRTOS RTC tick,
  // 32768 Hz on bare-metal builds.
  std::uint64_t (*ticks)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

// Packed for Slot::Clock — keep stable across reboots.
struct PersistBlob {
  std::uint32_t magic = 0u;
  std::uint32_t epoch_at_sync = 0u;
  std::uint64_t ticks_at_sync = 0u;
  std::int32_t drift_ppm = 0;  // applied to tick→second conversion
  std::uint32_t crc = 0u;
};

constexpr std::uint32_t kPersistMagic = 0x534C5443u;  // 'SLTC'

void init(const Hooks& hooks);
void load_persisted();
void save_persisted();

// Current Time Service (or test harness) sync — updates epoch + drift.
void apply_cts_sync(std::uint32_t unix_epoch_sec);

std::uint32_t unix_time();
Civil civil_now();
std::int32_t drift_ppm();
bool is_synced();

// Host/tests: force state without CTS.
void set_for_test(std::uint32_t epoch, std::int32_t ppm);

}  // namespace clock
}  // namespace slate
