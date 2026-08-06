#include "input_event.hpp"

#include <cstdint>

namespace {

const input::HitRect* g_rects = nullptr;
std::size_t g_rect_count = 0u;

// InfiniTime TouchHandler: one gesture per touch. The controller keeps
// reporting the same gesture byte while the finger is down, so without this a
// single slide fires repeatedly — and a slide or long-press reported with no
// finger on the panel is a stale register, not a gesture.
bool g_gesture_released = true;

bool is_continuous(cst816s::Gesture g) {
  switch (g) {
    case cst816s::Gesture::SlideDown:
    case cst816s::Gesture::SlideUp:
    case cst816s::Gesture::SlideLeft:
    case cst816s::Gesture::SlideRight:
    case cst816s::Gesture::LongPress:
      return true;
    default:
      return false;
  }
}

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
  g_gesture_released = true;
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

  // One gesture per touch, as TouchHandler::ProcessTouchInfo has it — but
  // WITHOUT InfiniTime's extra requirement that a slide or long-press still
  // have a finger on the panel.
  //
  // That requirement is sound at InfiniTime's timing, where the panel is read
  // within milliseconds of the interrupt. Slate reads it from the app loop,
  // which stalls up to ~1 s behind a full-face repaint (N-36), so by the time
  // poll() runs the finger has usually lifted and `touching` reads 0. Enforcing
  // it here silently discarded most swipes: 1018 interrupts produced 24 events,
  // and the launcher took several attempts to open.
  //
  // The latch alone is enough to stop a held gesture repeating: a fresh IRQ is
  // required to reach this code at all, and the gesture is consumed on accept.
  bool accept = false;
  if (sample.gesture != cst816s::Gesture::None && g_gesture_released) {
    accept = true;
    // Only a gesture the controller reports continuously needs latching; a tap
    // is a single event and re-arming immediately keeps double-taps working.
    g_gesture_released = !is_continuous(sample.gesture);
  }
  if (!sample.touching) {
    g_gesture_released = true;
  }
  if (!accept) {
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
