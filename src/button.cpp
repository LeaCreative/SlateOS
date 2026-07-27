#include "button.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"

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

void gpio_output_low(std::uint32_t pin) {
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT |
      nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << pin);
}

void gpio_input(std::uint32_t pin) {
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_INPUT |
      nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULLUP |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
}

bool strobe_read() {
  // Drive enable high, take kStableReads samples, drive enable low.
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << board::kButtonEnablePin);
  board::busy_wait_us(10u);

  std::uint32_t pressed_votes = 0u;
  for (std::uint32_t i = 0u; i < kStableReads; ++i) {
    const bool low =
        (nrf::reg<std::uint32_t>(nrf::gpio::IN) &
         (1u << board::kButtonSensePin)) == 0u;
    if (low) {
      ++pressed_votes;
    }
    board::busy_wait_us(5u);
  }

  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << board::kButtonEnablePin);
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
  gpio_output_low(board::kButtonEnablePin);
  gpio_input(board::kButtonSensePin);
  g_phase = Phase::Idle;
  g_saw_short = false;
  g_long_emitted = false;
}

bool raw_pressed() {
  return strobe_read();
}

Event poll() {
  const bool down = strobe_read();
  const std::uint32_t now = board::micros();

  switch (g_phase) {
    case Phase::Idle:
      if (down) {
        g_phase = Phase::DebounceDown;
        g_phase_start_us = now;
      } else if (g_saw_short &&
                 static_cast<std::int32_t>(now - g_phase_start_us) >
                     static_cast<std::int32_t>(kDoubleWindowUs)) {
        // Double window expired after a short — emit Press.
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
          // Long already reported; ignore the release.
          g_phase = Phase::Idle;
          g_saw_short = false;
        } else if (g_saw_short) {
          // Second short within the window.
          g_phase = Phase::Idle;
          g_saw_short = false;
          return make(Action::DoublePress);
        } else {
          // First short — wait for a possible second.
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
