#pragma once

#include <cstdint>

// Battery millivolts / percent + charge detect (P0.12 low = charging).
// Host injects ADC + GPIO reads.

namespace slate {
namespace battery {

struct Hooks {
  // Raw SAADC reading for AIN7, or -1 if unavailable.
  int (*read_adc_raw)(void* ctx) = nullptr;
  // True if charge indicator pin is asserted (charging).
  bool (*is_charging)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

void init(const Hooks& hooks);

// PineTime: mV = adc * 2000 / 1241 (roadmap).
std::uint16_t millivolts();
std::uint8_t percent();  // 0-100 rough linear map
bool charging();

}  // namespace battery
}  // namespace slate
