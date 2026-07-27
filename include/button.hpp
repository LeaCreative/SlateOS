#pragma once

#include <cstdint>

// Side button: enable P0.15, sense P0.13 (active-low when enabled).
// Leaving P0.15 high costs ~34 µA — strobe it for every sample.

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
