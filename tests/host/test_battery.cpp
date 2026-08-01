#include "battery.hpp"

#include <cstdio>

namespace {

int g_fails = 0;
int g_adc = -1;
bool g_charging = false;

void expect(const char* name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    ++g_fails;
  } else {
    std::printf("ok   %s\n", name);
  }
}

int read_adc(void*) { return g_adc; }
bool is_charging(void*) { return g_charging; }

// Invert Slate mV = adc * 4800 / 1024 (InfiniTime 10-bit, gain 1/4 — N-12).
int adc_for_mv(std::uint16_t mv) {
  return static_cast<int>((static_cast<std::uint32_t>(mv) * 1024u + 4799u) /
                          4800u);
}

void reset_hooks() {
  g_adc = -1;
  g_charging = false;
  slate::battery::Hooks h;
  h.read_adc_raw = &read_adc;
  h.is_charging = &is_charging;
  slate::battery::init(h);
}

}  // namespace

static void test_unknown() {
  reset_hooks();
  expect("unknown: !valid", !slate::battery::sample_valid());
  expect("unknown: percent FF",
         slate::battery::percent() == slate::battery::kPercentUnknown);
  expect("unknown: !percent_valid", !slate::battery::percent_valid());
}

static void test_curve_points() {
  expect("curve 3500→0", slate::battery::percent_from_mv(3500) == 0u);
  expect("curve 3616→3", slate::battery::percent_from_mv(3616) == 3u);
  expect("curve 3723→22", slate::battery::percent_from_mv(3723) == 22u);
  expect("curve 3776→48", slate::battery::percent_from_mv(3776) == 48u);
  expect("curve 3979→79", slate::battery::percent_from_mv(3979) == 79u);
  expect("curve 4180→100", slate::battery::percent_from_mv(4180) == 100u);
  expect("curve below→0", slate::battery::percent_from_mv(3000) == 0u);
  expect("curve above→100", slate::battery::percent_from_mv(4300) == 100u);
  // Mid-segment: halfway 3500–3616 ≈ 1%
  const std::uint8_t mid = slate::battery::percent_from_mv(3558);
  expect("curve mid 3500-3616", mid >= 1u && mid <= 2u);
}

static void test_charge_clamp() {
  expect(
      "clamp charging 100→99",
      slate::battery::apply_hysteresis(100u, 50u, true, true) == 99u);
  expect(
      "no clamp when not charging",
      slate::battery::apply_hysteresis(100u, 50u, false, false) == 100u);
}

static void test_hysteresis() {
  expect("charge only up",
         slate::battery::apply_hysteresis(40u, 60u, true, true) == 60u);
  expect("charge allow up",
         slate::battery::apply_hysteresis(70u, 60u, true, true) == 70u);
  expect("discharge only down",
         slate::battery::apply_hysteresis(80u, 60u, false, true) == 60u);
  expect("discharge allow down",
         slate::battery::apply_hysteresis(40u, 60u, false, true) == 40u);
  expect("first sample",
         slate::battery::apply_hysteresis(55u, 0u, false, false) == 55u);
}

static void test_percent_integration() {
  reset_hooks();
  g_adc = adc_for_mv(3776u);
  g_charging = false;
  expect("sample valid", slate::battery::sample_valid());
  const std::uint8_t p0 = slate::battery::percent();
  expect("first reading ~48", p0 >= 47u && p0 <= 49u);

  // Simulate noise upward while discharging — must not rise.
  g_adc = adc_for_mv(3979u);
  const std::uint8_t p1 = slate::battery::percent();
  expect("hysteresis holds while discharging", p1 == p0);

  g_charging = true;
  g_adc = adc_for_mv(4180u);
  const std::uint8_t p2 = slate::battery::percent();
  expect("charging clamp 99", p2 == 99u);

  g_charging = false;
  g_adc = adc_for_mv(4180u);
  const std::uint8_t p3 = slate::battery::percent();
  expect("unplugged can reach 100", p3 == 100u);
}

int main() {
  test_unknown();
  test_curve_points();
  test_charge_clamp();
  test_hysteresis();
  test_percent_integration();
  if (g_fails != 0) {
    std::printf("%d failures\n", g_fails);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
