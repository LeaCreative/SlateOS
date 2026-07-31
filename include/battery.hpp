#pragma once

#include <cstdint>

// Battery millivolts / percent + charge detect (P0.12 low = charging).
// Host injects ADC + GPIO reads. Percent mapping mirrors InfiniTime's
// BatteryController curve + charging clamp/hysteresis (low-level parity).

namespace slate {
namespace battery {

/** BAS / UI: not a real 0–100 reading (no SAADC sample yet). */
constexpr std::uint8_t kPercentUnknown = 0xFFu;

struct Hooks {
  // Raw SAADC reading for AIN7, or -1 if unavailable / not yet sampled.
  int (*read_adc_raw)(void* ctx) = nullptr;
  // True if charge indicator pin is asserted (charging).
  bool (*is_charging)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

void init(const Hooks& hooks);

// PineTime (roadmap): mV = adc * 2000 / 1241. InfiniTime uses a different
// SAADC gain path — validate this conversion with a multimeter before treating
// curve mV breakpoints as absolute. 0 if sample unknown.
std::uint16_t millivolts();

// 0–100 after curve + hysteresis, or kPercentUnknown if !sample_valid().
std::uint8_t percent();
bool percent_valid();  // true iff percent() is in 0..100

bool charging();
/** False until a real SAADC sample has been pushed via battery_hw. */
bool sample_valid();

// Host / unit-test surface (same math as percent() internals).
std::uint8_t percent_from_mv(std::uint16_t mv);
std::uint8_t apply_hysteresis(std::uint8_t raw_pct, std::uint8_t prev_pct,
                              bool charging, bool have_prev);

}  // namespace battery
}  // namespace slate
