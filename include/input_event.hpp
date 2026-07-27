#pragma once

#include "button.hpp"
#include "cst816s.hpp"
#include "hit_test.hpp"

#include <cstdint>

// Structured input events produced from touch + button.

namespace input {

// Wire §4.5 swipe_dir: UP=0 DOWN=1 LEFT=2 RIGHT=3
enum class SwipeDir : std::uint8_t {
  Up    = 0,
  Down  = 1,
  Left  = 2,
  Right = 3,
};

enum class EventType : std::uint8_t {
  None = 0,
  Tap,
  LongPress,
  Swipe,
  Button,
  MultiTap,
};

struct Event {
  EventType type = EventType::None;
  std::uint16_t x = 0u;
  std::uint16_t y = 0u;
  SwipeDir swipe = SwipeDir::Down;
  button::Action button = button::Action::None;
  std::uint16_t element_id = kNoHit;
  std::uint8_t multi_count = 0u;
};

void init();

// Optional hit-test table used to annotate Tap / LongPress with element_id.
void set_hit_rects(const HitRect* rects, std::size_t count);

// Poll touch IRQ + button; return one structured event (or None).
Event poll();

}  // namespace input
