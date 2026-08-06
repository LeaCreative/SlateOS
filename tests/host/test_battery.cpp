#include "battery.hpp"

#include <cstdio>

namespace {

int g_fails = 0;
int g_adc = -1;
bool g_charging = false;
bool g_power = false;

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
bool is_power_present(void*) { return g_power; }

// The cable going in asserts both pins; charge completing releases only the
// charging one (InfiniTime ReadPowerState).
void plug_in() {
  g_power = true;
  g_charging = true;
}
void charge_complete() {
  g_power = true;
  g_charging = false;
}
void unplug() {
  g_power = false;
  g_charging = false;
}

// Invert Slate mV = adc * 4800 / 1024 (InfiniTime 10-bit, gain 1/4 — N-12).
int adc_for_mv(std::uint16_t mv) {
  return static_cast<int>((static_cast<std::uint32_t>(mv) * 1024u + 4799u) /
                          4800u);
}

void reset_hooks() {
  g_adc = -1;
  unplug();
  slate::battery::Hooks h;
  h.read_adc_raw = &read_adc;
  h.is_charging = &is_charging;
  h.is_power_present = &is_power_present;
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
  // args: raw, prev, charging, power_present, full, have_prev
  expect("clamp charging 100→99",
         slate::battery::apply_hysteresis(100u, 50u, true, true, false, true) ==
             99u);
  expect(
      "no clamp when not charging",
      slate::battery::apply_hysteresis(100u, 50u, false, false, false, false) ==
          100u);
  // Charge complete with the cable still in: InfiniTime reports 100, not 99.
  expect("full reads 100",
         slate::battery::apply_hysteresis(88u, 99u, false, true, true, true) ==
             100u);
}

static void test_hysteresis() {
  expect("plugged only up",
         slate::battery::apply_hysteresis(40u, 60u, true, true, false, true) ==
             60u);
  expect("plugged allow up",
         slate::battery::apply_hysteresis(70u, 60u, true, true, false, true) ==
             70u);
  expect("unplugged only down",
         slate::battery::apply_hysteresis(80u, 60u, false, false, false,
                                          true) == 60u);
  expect("unplugged allow down",
         slate::battery::apply_hysteresis(40u, 60u, false, false, false,
                                          true) == 40u);
  expect("first sample",
         slate::battery::apply_hysteresis(55u, 0u, false, false, false,
                                          false) == 55u);
  // The direction gate follows power_present, not charging: a full battery
  // resting on the charger must not drift down.
  expect("full on charger holds",
         slate::battery::apply_hysteresis(95u, 100u, false, true, true, true) ==
             100u);
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

  plug_in();
  g_adc = adc_for_mv(4180u);
  const std::uint8_t p2 = slate::battery::percent();
  expect("charging clamp 99", p2 == 99u);

  // Charger stops pushing current with the cable still in → full.
  charge_complete();
  const std::uint8_t p3 = slate::battery::percent();
  expect("charge complete reads 100", p3 == 100u);
  expect("full flag set", slate::battery::full());

  // Cable out: full clears, and the reading may only fall from here.
  unplug();
  g_adc = adc_for_mv(3979u);
  const std::uint8_t p4 = slate::battery::percent();
  expect("full flag clears on unplug", !slate::battery::full());
  expect("discharging falls", p4 < p3);
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
