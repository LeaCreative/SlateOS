#include "button.hpp"
#include "board.hpp"

#include <cstdint>

namespace {

constexpr std::uint32_t kDebounceUs     = 20000u;   // 20 ms
constexpr std::uint32_t kLongPressUs    = 500000u;  // 500 ms
constexpr std::uint32_t kDoubleWindowUs = 350000u;  // 350 ms
constexpr std::uint32_t kStableReads    = 4u;

enum class Phase : std::uint8_t {
  Idle,
  DebounceDown,
  Held,
  DebounceUp,
  WaitDouble,
};

Phase g_phase = Phase::Idle;
std::uint32_t g_phase_start_us = 0u;
std::uint32_t g_press_start_us = 0u;
bool g_long_emitted = false;
bool g_saw_short = false;

bool sample_pressed() {
  // Enable stays high (InfiniTime). Majority vote for debounce noise only.
  std::uint32_t pressed_votes = 0u;
  for (std::uint32_t i = 0u; i < kStableReads; ++i) {
    if (board::button_raw()) {
      ++pressed_votes;
    }
  }
  return pressed_votes >= (kStableReads / 2u + 1u);
}

button::Event make(button::Action action) {
  button::Event e;
  e.action = action;
  e.valid = true;
  return e;
}

}  // namespace

namespace button {

void init() {
  board::button_hw_init();
  g_phase = Phase::Idle;
  g_saw_short = false;
  g_long_emitted = false;
}

bool raw_pressed() {
  return board::button_raw();
}

Event poll() {
  const bool down = sample_pressed();
  // micros() wrap is mod 2^32. Casting (now - start) through int32_t is safe
  // for intervals < 2^31 µs (~35 min) — all button phases are ≪ that. Do not
  // use micros()/1000 for session-scale clocks (see slate::time::mono_ms).
  const std::uint32_t now = board::micros();

  switch (g_phase) {
    case Phase::Idle:
      if (down) {
        g_phase = Phase::DebounceDown;
        g_phase_start_us = now;
      } else if (g_saw_short &&
                 static_cast<std::int32_t>(now - g_phase_start_us) >
                     static_cast<std::int32_t>(kDoubleWindowUs)) {
        g_saw_short = false;
        return make(Action::Press);
      }
      break;

    case Phase::DebounceDown:
      if (!down) {
        g_phase = Phase::Idle;
      } else if (static_cast<std::int32_t>(now - g_phase_start_us) >
                 static_cast<std::int32_t>(kDebounceUs)) {
        g_phase = Phase::Held;
        g_press_start_us = g_phase_start_us;
        g_long_emitted = false;
      }
      break;

    case Phase::Held:
      if (!down) {
        g_phase = Phase::DebounceUp;
        g_phase_start_us = now;
      } else if (!g_long_emitted &&
                 static_cast<std::int32_t>(now - g_press_start_us) >
                     static_cast<std::int32_t>(kLongPressUs)) {
        g_long_emitted = true;
        g_saw_short = false;
        return make(Action::LongPress);
      }
      break;

    case Phase::DebounceUp:
      if (down) {
        g_phase = Phase::Held;
      } else if (static_cast<std::int32_t>(now - g_phase_start_us) >
                 static_cast<std::int32_t>(kDebounceUs)) {
        if (g_long_emitted) {
          g_phase = Phase::Idle;
          g_saw_short = false;
        } else if (g_saw_short) {
          g_phase = Phase::Idle;
          g_saw_short = false;
          return make(Action::DoublePress);
        } else {
          g_saw_short = true;
          g_phase = Phase::WaitDouble;
          g_phase_start_us = now;
        }
      }
      break;

    case Phase::WaitDouble:
      if (down) {
        g_phase = Phase::DebounceDown;
        g_phase_start_us = now;
      } else if (static_cast<std::int32_t>(now - g_phase_start_us) >
                 static_cast<std::int32_t>(kDoubleWindowUs)) {
        g_phase = Phase::Idle;
        g_saw_short = false;
        return make(Action::Press);
      }
      break;
  }

  return Event{};
}

}  // namespace button
