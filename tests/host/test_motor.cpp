// Vibration sequencer (InfiniTime MotorController port). The point of the
// driver is that nothing blocks: every pattern is a series of timer re-arms,
// so what is worth asserting on the host is the shape of that series and the
// fail-safe — an action that does not schedule must leave the motor off.

#include "motor.hpp"
#include "sdp_opcodes.hpp"

#include <cstdio>
#include <vector>

namespace {

int g_fails = 0;

void expect(const char* name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    ++g_fails;
  } else {
    std::printf("ok   %s\n", name);
  }
}

struct Step {
  bool motor_on;
  std::uint16_t delay_ms;
};

// Drive a pattern to completion the way the FreeRTOS timer would, recording
// each armed interval. Bounded so a sequencer that never finishes fails the
// test rather than hanging it.
std::vector<Step> run(std::uint8_t pattern, bool* finished) {
  slate::motor::Seq seq;
  std::vector<Step> steps;
  slate::motor::Seq::Action a = seq.begin(slate::motor::pattern_desc(pattern));
  *finished = false;
  for (int guard = 0; guard < 32; ++guard) {
    if (!a.schedule) {
      *finished = true;
      // The terminal action always drives the motor off.
      expect("terminal action leaves motor off", !a.motor_on);
      break;
    }
    steps.push_back(Step{a.motor_on, a.delay_ms});
    a = seq.expire();
  }
  return steps;
}

std::uint32_t total_on_ms(const std::vector<Step>& steps) {
  std::uint32_t sum = 0u;
  for (const Step& s : steps) {
    if (s.motor_on) {
      sum += s.delay_ms;
    }
  }
  return sum;
}

}  // namespace

static void test_single_pulse() {
  bool done = false;
  const std::vector<Step> steps = run(sdp::haptic_pattern::TICK, &done);
  expect("TICK finishes", done);
  expect("TICK is one step", steps.size() == 1u);
  expect("TICK drives the motor on", !steps.empty() && steps[0].motor_on);
  expect("TICK is 20 ms", !steps.empty() && steps[0].delay_ms == 20u);
}

static void test_double_pulse() {
  bool done = false;
  const std::vector<Step> steps = run(sdp::haptic_pattern::DOUBLE, &done);
  expect("DOUBLE finishes", done);
  // on 25 → gap 40 (off) → on 25, then terminal off.
  expect("DOUBLE is three steps", steps.size() == 3u);
  if (steps.size() == 3u) {
    expect("DOUBLE pulse 1 on 25 ms",
           steps[0].motor_on && steps[0].delay_ms == 25u);
    expect("DOUBLE gap is off for 40 ms",
           !steps[1].motor_on && steps[1].delay_ms == 40u);
    expect("DOUBLE pulse 2 on 25 ms",
           steps[2].motor_on && steps[2].delay_ms == 25u);
  }
  expect("DOUBLE runs the motor for 50 ms", total_on_ms(steps) == 50u);
}

static void test_triple_long() {
  bool done = false;
  const std::vector<Step> steps = run(sdp::haptic_pattern::TRIPLE_LONG, &done);
  expect("TRIPLE_LONG finishes", done);
  // on 120, gap 80, on 120, gap 80, on 120
  expect("TRIPLE_LONG is five steps", steps.size() == 5u);
  expect("TRIPLE_LONG on-time 360 ms", total_on_ms(steps) == 360u);
}

static void test_every_pattern_terminates() {
  for (std::uint8_t p = 0u; p <= sdp::haptic_pattern::Max; ++p) {
    bool done = false;
    const std::vector<Step> steps = run(p, &done);
    expect("pattern terminates", done);
    expect("pattern does something", !steps.empty());
    expect("pattern starts by driving the motor",
           !steps.empty() && steps[0].motor_on);
  }
  // Out-of-range ids are rejected by the parser, but the table must still be
  // total — a haptic that never ends would leave the motor running.
  bool done = false;
  (void)run(0xFFu, &done);
  expect("unknown pattern terminates", done);
}

static void test_cancel_and_idle() {
  slate::motor::Seq seq;
  slate::motor::Seq::Action a =
      seq.begin(slate::motor::pattern_desc(sdp::haptic_pattern::DOUBLE));
  expect("DOUBLE begins active", seq.active() && a.schedule);
  seq.cancel();
  expect("cancel clears active", !seq.active());
  a = seq.expire();
  expect("expire after cancel does not re-arm", !a.schedule && !a.motor_on);

  // A late timer callback on an idle sequencer must not restart the motor.
  slate::motor::Seq idle;
  a = idle.expire();
  expect("idle expire is inert", !a.schedule && !a.motor_on);
}

static void test_degenerate_descriptors() {
  slate::motor::Seq seq;
  slate::motor::Seq::Action a =
      seq.begin(slate::motor::PatternDesc{0u, 1u, 0u});
  expect("zero-length pulse arms nothing", !a.schedule && !a.motor_on);
  expect("zero-length pulse leaves the sequencer idle", !seq.active());

  a = seq.begin(slate::motor::PatternDesc{20u, 0u, 0u});
  expect("zero pulses arms nothing", !a.schedule && !a.motor_on);

  // Two pulses with no gap would read as one long buzz; collapse to one.
  a = seq.begin(slate::motor::PatternDesc{20u, 2u, 0u});
  expect("gapless repeat starts", a.schedule && a.motor_on);
  a = seq.expire();
  expect("gapless repeat is a single pulse", !a.schedule && !a.motor_on);
}

int main() {
  test_single_pulse();
  test_double_pulse();
  test_triple_long();
  test_every_pattern_terminates();
  test_cancel_and_idle();
  test_degenerate_descriptors();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all motor tests passed\n");
  return 0;
}
