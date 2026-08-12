// Shake-to-wake host tests (InfiniTime CurrentShakeSpeed EMA).

#include "shake_wake.hpp"

#include <cstdio>

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

using slate::motion::ShakeDetector;
using slate::motion::Sensitivity;
using slate::motion::shake_threshold;

void fill_history(ShakeDetector& d, std::int16_t x, std::int16_t y,
                  std::int16_t z) {
  for (std::size_t i = 0; i < ShakeDetector::kHistory; ++i) {
    d.update(x, y, z);
  }
}

void test_thresholds() {
  expect("soft < normal", shake_threshold(Sensitivity::Soft) <
                              shake_threshold(Sensitivity::Normal));
  expect("normal < hard", shake_threshold(Sensitivity::Normal) <
                              shake_threshold(Sensitivity::Hard));
  expect("normal is InfiniTime default",
         shake_threshold(Sensitivity::Normal) == 150);
}

void test_still_does_not_wake() {
  ShakeDetector d;
  d.reset();
  d.set_sensitivity(Sensitivity::Soft);
  fill_history(d, 0, 0, -1024);
  expect("still Soft does not wake", !d.should_shake_wake());
}

void test_flick_wakes_soft_not_hard() {
  ShakeDetector soft;
  soft.reset();
  soft.set_sensitivity(Sensitivity::Soft);
  fill_history(soft, 0, 0, -1024);
  // Large Δz over the window — a sharp flick.
  for (std::size_t i = 0; i < ShakeDetector::kHistory; ++i) {
    soft.update(0, 0, static_cast<std::int16_t>(-1024 + static_cast<int>(i) * 200));
  }
  expect("flick wakes Soft", soft.should_shake_wake());

  ShakeDetector hard;
  hard.reset();
  hard.set_sensitivity(Sensitivity::Hard);
  fill_history(hard, 0, 0, -1024);
  // Milder motion — Soft threshold 80, Hard 250.
  for (std::size_t i = 0; i < ShakeDetector::kHistory; ++i) {
    hard.update(0, 0, static_cast<std::int16_t>(-1024 + static_cast<int>(i) * 40));
  }
  expect("mild motion does not wake Hard", !hard.should_shake_wake());
}

void test_reset_clears_speed() {
  ShakeDetector d;
  d.set_sensitivity(Sensitivity::Soft);
  fill_history(d, 0, 0, -1024);
  for (std::size_t i = 0; i < ShakeDetector::kHistory; ++i) {
    d.update(0, 0, static_cast<std::int16_t>(-1024 + static_cast<int>(i) * 200));
  }
  expect("precondition: would wake", d.should_shake_wake());
  d.reset();
  expect("after reset speed is 0", d.speed() == 0);
  expect("after reset does not wake", !d.should_shake_wake());
}

}  // namespace

int main() {
  test_thresholds();
  test_still_does_not_wake();
  test_flick_wakes_soft_not_hard();
  test_reset_clears_speed();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all shake-wake tests passed\n");
  return 0;
}
