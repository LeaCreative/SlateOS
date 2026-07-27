#include "input_event.hpp"

#include <cstdint>

namespace {

const input::HitRect* g_rects = nullptr;
std::size_t g_rect_count = 0u;

input::SwipeDir gesture_to_swipe(cst816s::Gesture g) {
  switch (g) {
    case cst816s::Gesture::SlideUp:    return input::SwipeDir::Up;
    case cst816s::Gesture::SlideDown:  return input::SwipeDir::Down;
    case cst816s::Gesture::SlideLeft:  return input::SwipeDir::Left;
    case cst816s::Gesture::SlideRight: return input::SwipeDir::Right;
    default:                           return input::SwipeDir::Down;
  }
}

}  // namespace

namespace input {

void init() {
  g_rects = nullptr;
  g_rect_count = 0u;
}

void set_hit_rects(const HitRect* rects, std::size_t count) {
  g_rects = rects;
  g_rect_count = count;
}

Event poll() {
  Event ev{};

  {
    const button::Event be = button::poll();
    if (be.valid && be.action != button::Action::None) {
      ev.type = EventType::Button;
      ev.button = be.action;
      return ev;
    }
  }

  if (!cst816s::irq_pending()) {
    return ev;
  }

  const cst816s::TouchSample sample = cst816s::poll();
  if (!sample.valid) {
    return ev;
  }

  ev.x = sample.x;
  ev.y = sample.y;

  switch (sample.gesture) {
    case cst816s::Gesture::SingleTap:
      ev.type = EventType::Tap;
      ev.element_id = hit_test(sample.x, sample.y, g_rects, g_rect_count);
      break;
    case cst816s::Gesture::DoubleTap:
      ev.type = EventType::MultiTap;
      ev.multi_count = 2u;
      ev.element_id = hit_test(sample.x, sample.y, g_rects, g_rect_count);
      break;
    case cst816s::Gesture::LongPress:
      ev.type = EventType::LongPress;
      ev.element_id = hit_test(sample.x, sample.y, g_rects, g_rect_count);
      break;
    case cst816s::Gesture::SlideDown:
    case cst816s::Gesture::SlideUp:
    case cst816s::Gesture::SlideLeft:
    case cst816s::Gesture::SlideRight:
      ev.type = EventType::Swipe;
      ev.swipe = gesture_to_swipe(sample.gesture);
      break;
    case cst816s::Gesture::None:
    default:
      break;
  }

  return ev;
}

}  // namespace input
