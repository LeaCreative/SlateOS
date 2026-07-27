#pragma once

#include "button.hpp"
#include "cst816s.hpp"
#include "hit_test.hpp"

#include <cstdint>

// Structured input events produced from touch + button.

namespace input {

enum class SwipeDir : std::uint8_t {
  Down  = 0,
  Up    = 1,
  Left  = 2,
  Right = 3,
};

enum class EventType : std::uint8_t {
  None = 0,
  Tap,
  LongPress,
  Swipe,
  Button,
};

struct Event {
  EventType type = EventType::None;
  std::uint16_t x = 0u;
  std::uint16_t y = 0u;
  SwipeDir swipe = SwipeDir::Down;
  button::Action button = button::Action::None;
  std::uint16_t element_id = kNoHit;  // filled when hit-test table is set
};

void init();

// Optional hit-test table used to annotate Tap / LongPress with element_id.
void set_hit_rects(const HitRect* rects, std::size_t count);

// Poll touch IRQ + button; return one structured event (or None).
Event poll();

}  // namespace input
