// Raise-to-wake, on the desktop.
//
// The whole point of keeping the detector free of drivers: a gesture can be
// written down as a sequence of accelerometer samples and replayed here, so
// "does a wrist flick wake it" and "does typing wake it" are questions with
// answers before anything is flashed to a sealed watch.
//
// Counts are +/-2g where 1g is about 1024, matching the BMA at its configured
// range.

#include "raise_wake.hpp"

#include <cstdio>
#include <cstdlib>

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

constexpr std::int16_t kG = 1024;

using slate::motion::RaiseDetector;

/** Hold one orientation until the history is full of it. */
void settle(RaiseDetector& d, std::int16_t x, std::int16_t y, std::int16_t z) {
  for (std::size_t i = 0; i < RaiseDetector::kHistory * 2u; ++i) {
    d.update(x, y, z);
  }
}

// ── asin ─────────────────────────────────────────────────────────────────────

void test_asin() {
  using slate::motion::asin_degrees;
  expect("asin(0) == 0", asin_degrees(0) == 0);
  expect("asin(full) == 90", asin_degrees(32767) == 90);
  expect("asin(-full) == -90", asin_degrees(-32767) == -90);
  // sin(30) = 0.5
  const std::int16_t a30 = asin_degrees(16384);
  expect("asin(0.5) ~ 30", a30 >= 29 && a30 <= 31);
  const std::int16_t a45 = asin_degrees(23170);
  expect("asin(0.707) ~ 45", a45 >= 44 && a45 <= 46);
  expect("asin is odd", asin_degrees(-16384) == -a30);
  // Out of range must clamp, not run off the table.
  expect("asin clamps above full scale", asin_degrees(100000) == 90);
  expect("asin clamps below full scale", asin_degrees(-100000) == -90);
}

// ── the detector ─────────────────────────────────────────────────────────────

/**
 * The gesture: arm hanging by the side, then rotated up to read the watch.
 *
 * The orientations are not arbitrary, and getting them wrong is easy — the
 * first attempt at this test started with z at exactly 0, which lands in the
 * quadrant branch of degrees_rolled() that returns +86 degrees and never fires.
 * What the algorithm actually looks for:
 *
 *  - arm down: gravity mostly along +y (towards 6 o'clock on the face), with z
 *    already somewhat negative, because a hanging wrist tilts the face
 *  - reading: gravity almost entirely along -z, face up, y near zero
 *
 * Both z values are negative, so degrees_rolled takes `angle - prev_angle`:
 * asin(-75*32) - asin(900*32) is about -4 - 61 = -65 degrees, past the -45
 * threshold. That is the gesture.
 */
void test_raise_gesture_wakes() {
  RaiseDetector d;
  d.reset();
  settle(d, 0, 900, -300);  // arm down, face tilted outwards
  expect("arm hanging still does not wake", !d.should_raise_wake());
  // Six samples of rotation — the history is 8, so the oldest two are still
  // the hanging position when the newest two are the reading position.
  d.update(0, 700, -600);
  d.update(0, 400, -800);
  d.update(0, 150, -950);
  d.update(0, -20, -1010);
  d.update(0, -80, -1020);
  d.update(0, -70, -1020);
  expect("raise gesture wakes", d.should_raise_wake());
}

/** Sitting perfectly still must never wake, whatever the orientation. */
void test_still_does_not_wake() {
  const std::int16_t orientations[][3] = {
      {0, -kG, 0}, {0, 0, -kG}, {0, 0, kG}, {kG, 0, 0}, {-kG, 0, 0}, {0, kG, 0},
  };
  bool any = false;
  for (const auto& o : orientations) {
    RaiseDetector d;
    d.reset();
    settle(d, o[0], o[1], o[2]);
    if (d.should_raise_wake()) {
      std::printf("  woke while still at (%d,%d,%d)\n", o[0], o[1], o[2]);
      any = true;
    }
  }
  expect("no orientation wakes while still", !any);
}

/**
 * Shaking, typing, walking: high variance. These are the false positives that
 * make a wrist-wake feature intolerable, and the variance test is what rejects
 * them.
 */
void test_agitation_does_not_wake() {
  RaiseDetector d;
  d.reset();
  settle(d, 0, -kG, 0);
  bool woke = false;
  for (int i = 0; i < 60; ++i) {
    const std::int16_t jitter = static_cast<std::int16_t>((i % 2) ? 400 : -400);
    d.update(jitter, static_cast<std::int16_t>(-kG + jitter), jitter);
    if (d.should_raise_wake()) {
      woke = true;
      break;
    }
  }
  expect("agitation does not wake", !woke);
}

/** An arm held out sideways is not a look at the watch. */
void test_arm_not_level_does_not_wake() {
  RaiseDetector d;
  d.reset();
  settle(d, kG, -200, 0);
  d.update(kG, -150, -300);
  d.update(kG, -100, -600);
  d.update(kG, -80, -900);
  d.update(kG, -70, -1000);
  expect("x-axis rejection holds", !d.should_raise_wake());
}

/**
 * A fresh detector must not fire before it has real history. Otherwise the
 * watch wakes on the first movement after every single sleep.
 */
void test_cold_start_does_not_wake() {
  RaiseDetector d;
  d.reset();
  bool woke = false;
  for (std::size_t i = 0; i < RaiseDetector::kHistory - 1u; ++i) {
    d.update(0, -70, -1020);
    if (d.should_raise_wake()) {
      woke = true;
    }
  }
  expect("cold start cannot wake", !woke);
}

/** Reset must actually clear, or a stale window survives into the next wake. */
void test_reset_clears_history() {
  RaiseDetector d;
  settle(d, 0, -kG, 0);
  d.update(0, -70, -1020);
  d.reset();
  expect("reset re-arms the fill guard", !d.should_raise_wake());
}

/** The stats window really is newest-two against oldest-two. */
void test_stats_window() {
  RaiseDetector d;
  d.reset();
  for (std::int16_t i = 0; i < 8; ++i) {
    d.update(static_cast<std::int16_t>(i * 100), 0, 0);
  }
  const auto s = d.stats();
  // Newest two are 600 and 700 -> 650. Oldest two are 0 and 100 -> 50.
  expect("mean is the newest window", s.x_mean == 650);
  expect("prev mean is the oldest window", s.prev_x_mean == 50);
}

/** Soft fires where Hard rejects (look-angle gate + roll). */
void test_soft_vs_hard_roll() {
  // Reading pose with y ≈ −50: Soft (y_mean ≤ 0) accepts; Hard (y_mean ≤ −128)
  // rejects. Roll is still past Soft's −30° threshold.
  auto feed = [](RaiseDetector& d) {
    settle(d, 0, 900, -300);
    d.update(0, 500, -600);
    d.update(0, 200, -850);
    d.update(0, 50, -980);
    d.update(0, -20, -1010);
    d.update(0, -50, -1020);
    d.update(0, -50, -1020);
  };

  RaiseDetector soft;
  soft.reset();
  soft.set_sensitivity(slate::motion::Sensitivity::Soft);
  feed(soft);
  expect("Soft wakes on milder look angle", soft.should_raise_wake());

  RaiseDetector hard;
  hard.reset();
  hard.set_sensitivity(slate::motion::Sensitivity::Hard);
  feed(hard);
  expect("Hard rejects milder look angle", !hard.should_raise_wake());
}

}  // namespace

int main() {
  test_asin();
  test_raise_gesture_wakes();
  test_still_does_not_wake();
  test_agitation_does_not_wake();
  test_arm_not_level_does_not_wake();
  test_cold_start_does_not_wake();
  test_reset_clears_history();
  test_stats_window();
  test_soft_vs_hard_roll();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all raise-wake tests passed\n");
  return 0;
}
