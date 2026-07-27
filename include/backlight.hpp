#pragma once

#include <cstdint>

// Backlight controller for the PineTime.
//
// The PineTime has three active-low backlight pins:
//   LOW  = P0.14
//   MID  = P0.22
//   HIGH = P0.23
//
// Driving a pin low turns the corresponding LED string on.
// PWM0 is used to generate a variable duty-cycle signal on these pins so we
// can set brightness 0-100 rather than just on/off.
//
// The nRF52832 PWM peripheral works with an internal counter that counts up
// to COUNTERTOP.  Each channel has a compare value in a sequence buffer;
// when the counter equals the value the output is toggled.  Setting bit15 of
// the compare word inverts the output polarity (active-low compensation).
//
// We use DECODER=INDIVIDUAL so all four PWM channels have their own compare
// value, though we only use three of them.

namespace backlight {

// Initialise PWM0 and configure the three backlight output pins.
// After init() the backlight is off (brightness 0).
void init();

// Set brightness 0-100 (percent).  0 = off, 100 = full brightness.
void set(std::uint32_t percent);

// Convenience shortcuts.
void off();
void on_low();
void on_mid();
void on_high();

}  // namespace backlight
