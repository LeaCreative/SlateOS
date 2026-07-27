#pragma once

#include <cstdint>

namespace board {

constexpr std::uint32_t kMotorPin = 16u;
constexpr std::uint32_t kButtonEnablePin = 15u;  // drive high to read
constexpr std::uint32_t kButtonSensePin  = 13u;  // pressed = low when enabled

void busy_wait_us(std::uint32_t microseconds);
void busy_wait_ms(std::uint32_t milliseconds);
void motor_on();
void motor_off();
void pulse_motor(std::uint32_t milliseconds);

// Microsecond timestamp from TIMER0 (1 MHz free-running).
std::uint32_t micros();

}  // namespace board
