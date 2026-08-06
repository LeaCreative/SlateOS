#pragma once

#include <cstdint>

/**
 * Haptic motor — InfiniTime `components/motor/MotorController` ported.
 *
 * InfiniTime never blocks for a vibration: `RunForDuration()` starts a one-shot
 * FreeRTOS timer, drives the pin, and returns. Slate used to busy-wait inside
 * `board::pulse_motor()` on the app task — up to 90 ms for a DOUBLE (25+40+25)
 * — and that task also drains the SDP inbox, so every haptic directly widened
 * the stall behind the inbox drops (P-8 / N-35). Nothing here waits.
 *
 * The pin is active-LOW (`board::motor_on()` clears it). A failed timer start
 * must never leave the motor running, so the pin is only driven on after the
 * timer has been armed — same order as InfiniTime.
 */
namespace slate {
namespace motor {

/** One pattern, expanded: `pulses` bursts of `on_ms`, separated by `gap_ms`. */
struct PatternDesc {
  std::uint16_t on_ms;
  std::uint8_t pulses;
  std::uint16_t gap_ms;
};

/**
 * SDP haptic patterns (`sdp::haptic_pattern`) as motor timings.
 *
 * One table for every caller. There were two before — `main.cpp::do_haptic`
 * (30 / 30 / 25+40+25 / 80 / 30 ms) and the interpreter's `SideEffectSink`
 * (20 / 40 / 40+40+40 / 120 / 80 ms) — so the same pattern id felt different
 * depending on whether the phone or the local core raised it.
 */
constexpr PatternDesc pattern_desc(std::uint8_t pattern) {
  // Values are sdp::haptic_pattern; duplicated as literals so this header stays
  // free of SDP wire constants (they live only in sdp_opcodes.hpp).
  switch (pattern) {
    case 0u:  return PatternDesc{20u, 1u, 0u};    // TICK
    case 1u:  return PatternDesc{40u, 1u, 0u};    // SHORT
    case 2u:  return PatternDesc{25u, 2u, 40u};   // DOUBLE
    case 3u:  return PatternDesc{120u, 1u, 0u};   // LONG
    case 4u:  return PatternDesc{80u, 1u, 0u};    // ERROR
    default:  return PatternDesc{30u, 1u, 0u};
  }
}

/**
 * Pattern state machine, free of FreeRTOS so it can be host-tested.
 *
 * The driver owns one one-shot timer and re-arms it; `expire()` says what to do
 * each time it fires. InfiniTime only ever needs a single pulse, so it has no
 * equivalent — DOUBLE is a Slate pattern and this is the non-blocking way to
 * express it.
 */
class Seq {
 public:
  struct Action {
    bool motor_on;      ///< desired pin state once `delay_ms` is armed
    bool schedule;      ///< arm the one-shot for `delay_ms`
    std::uint16_t delay_ms;
  };

  /** Start a pattern, replacing anything in flight. */
  Action begin(const PatternDesc& d) {
    if (d.on_ms == 0u || d.pulses == 0u) {
      running_ = false;
      in_gap_ = false;
      remaining_ = 0u;
      return Action{false, false, 0u};
    }
    on_ms_ = d.on_ms;
    gap_ms_ = d.gap_ms;
    remaining_ = static_cast<std::uint8_t>(d.pulses - 1u);
    // A gap of zero would merge the bursts into one long buzz.
    if (remaining_ > 0u && gap_ms_ == 0u) {
      remaining_ = 0u;
    }
    in_gap_ = false;
    running_ = true;
    return Action{true, true, on_ms_};
  }

  /** The one-shot fired. */
  Action expire() {
    if (!running_) {
      return Action{false, false, 0u};
    }
    if (in_gap_) {
      in_gap_ = false;
      return Action{true, true, on_ms_};
    }
    if (remaining_ > 0u) {
      --remaining_;
      in_gap_ = true;
      return Action{false, true, gap_ms_};
    }
    running_ = false;
    return Action{false, false, 0u};
  }

  /** Abandon the sequence; the caller drives the pin off. */
  void cancel() {
    running_ = false;
    in_gap_ = false;
    remaining_ = 0u;
  }

  bool active() const { return running_; }

 private:
  std::uint16_t on_ms_ = 0u;
  std::uint16_t gap_ms_ = 0u;
  std::uint8_t remaining_ = 0u;
  bool in_gap_ = false;
  bool running_ = false;
};

/**
 * Configure the pin and create the one-shot timer.
 *
 * Call once from main() before the scheduler starts — `xTimerCreate` is legal
 * there, and a start queued before `vTaskStartScheduler()` runs when the timer
 * daemon does.
 */
void init();

/** InfiniTime `RunForDuration` — one non-blocking pulse. 0 ms is a no-op. */
void run_for_duration(std::uint16_t ms);

/** Play an `sdp::haptic_pattern`. Returns immediately; never blocks. */
void play(std::uint8_t pattern);

/** Cancel any sequence and drive the motor off now. */
void stop();

/** True while a pattern is still running. */
bool busy();

}  // namespace motor
}  // namespace slate
