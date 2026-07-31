#pragma once

#include <cstdint>

// Side button: InfiniTime-style — enable P0.15 left high, sense P0.13
// (active-high, pulldown). Pressed = HIGH.

namespace button {

enum class Action : std::uint8_t {
  None = 0,
  Press,       // short press released
  LongPress,   // held past long threshold
  DoublePress, // two short presses within the double window
};

struct Event {
  Action action = Action::None;
  bool valid = false;
};

void init();

// Call every few ms from the main loop.  Returns an event when one completes.
Event poll();

// Raw strobed read (true = pressed), for diagnostics.
bool raw_pressed();

}  // namespace button
