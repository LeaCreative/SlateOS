#pragma once

#include "session_profiles.hpp"

#include <cstdint>

// §8 power policy + Gate C instrumentation.
// Transitions are logged (when SLATE_POWER_INSTRUM=1) with TIMER1 timestamps
// (board::micros) so a PPK II trace can be correlated against behaviour.

namespace slate {
namespace power {

enum class State : std::uint8_t {
  Ambient = 0,   // backlight off, long interval, peripherals asleep
  Glance  = 1,   // short lit wake
  Active  = 2,   // interactive session
  Streaming = 3, // patch / camera tier
};

struct Hooks {
  // Request BLE connection-interval update (1.25 ms units). May be null.
  bool (*request_conn_interval)(std::uint16_t interval_units, void* ctx) = nullptr;
  void* ctx = nullptr;
};

void init(const Hooks& hooks);

// Map session profile → power state and apply peripherals + radio.
void apply_profile(const profile::Desc& desc);

void enter(State s);
State current();

// Disable SPIM0 / TWIM1 when nothing needs the bus (caller must not hold locks).
void buses_idle();
void buses_wake();

// Sample battery via SAADC then TASKS_STOP (not merely ENABLE=0).
int sample_battery_adc();

// Put HRS3300 to sleep (PDRIVER=0). Prefer hrs::Driver::disable() when the
// driver owns the chip; this remains as a boot fallback before the driver init.
void hrs_sleep();

// Enter System ON idle until RTC2 compare or PIN DETECT (touch).
// Replaces busy-wait in the main loop — largest ambient win without NimBLE.
void sleep_ms(std::uint32_t max_ms);

// Prefer LOWPOWER over CONSTLAT when measuring / shipping.
void prefer_low_power();

// Soft request to leave debug-friendly constant latency (docs/power.md).
void disable_debug_assist();

}  // namespace power
}  // namespace slate
