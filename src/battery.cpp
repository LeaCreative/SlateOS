#include "battery.hpp"

namespace slate {
namespace battery {
namespace {

Hooks g_hooks{};
std::uint8_t g_displayed = kPercentUnknown;
bool g_have_displayed = false;
bool g_was_charging = false;

// InfiniTime BatteryController voltage→% lookup (mV). Piecewise linear.
struct Point {
  std::uint16_t mv;
  std::uint8_t pct;
};
constexpr Point kCurve[] = {
    {3500u, 0u},  {3616u, 3u},  {3723u, 22u},
    {3776u, 48u}, {3979u, 79u}, {4180u, 100u},
};
constexpr std::uint8_t kCurveN =
    static_cast<std::uint8_t>(sizeof(kCurve) / sizeof(kCurve[0]));

}  // namespace

void init(const Hooks& hooks) {
  g_hooks = hooks;
  g_displayed = kPercentUnknown;
  g_have_displayed = false;
  g_was_charging = false;
}

std::uint16_t millivolts() {
  if (g_hooks.read_adc_raw == nullptr) {
    return 0u;
  }
  const int adc = g_hooks.read_adc_raw(g_hooks.ctx);
  if (adc < 0) {
    return 0u;
  }
  // InfiniTime BatteryController: mV = raw * 8 * 600 / 1024, i.e. the 10-bit
  // count scaled by the internal 0.6 V reference over gain 1/4 (2.4 V full
  // scale) and doubled for the 1:2 divider. Matches the SAADC setup in
  // power.cpp::sample_battery_adc — change the two together (N-12).
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(adc) * 4800u) /
                                    1024u);
}

std::uint8_t percent_from_mv(std::uint16_t mv) {
  if (mv <= kCurve[0].mv) {
    return kCurve[0].pct;
  }
  if (mv >= kCurve[kCurveN - 1u].mv) {
    return kCurve[kCurveN - 1u].pct;
  }
  for (std::uint8_t i = 0u; i + 1u < kCurveN; ++i) {
    const Point a = kCurve[i];
    const Point b = kCurve[i + 1u];
    if (mv > b.mv) {
      continue;
    }
    const std::uint32_t span = static_cast<std::uint32_t>(b.mv - a.mv);
    const std::uint32_t off = static_cast<std::uint32_t>(mv - a.mv);
    const std::uint32_t dp =
        static_cast<std::uint32_t>(b.pct) - static_cast<std::uint32_t>(a.pct);
    return static_cast<std::uint8_t>(
        static_cast<std::uint32_t>(a.pct) + (off * dp) / span);
  }
  return kCurve[kCurveN - 1u].pct;
}

std::uint8_t apply_hysteresis(std::uint8_t raw_pct, std::uint8_t prev_pct,
                              bool is_charging, bool have_prev) {
  std::uint8_t raw = raw_pct;
  // InfiniTime: while charging, UI maxes at 99 until charge completes.
  if (is_charging && raw > 99u) {
    raw = 99u;
  }
  if (!have_prev || prev_pct > 100u) {
    return raw;
  }
  if (is_charging) {
    return raw > prev_pct ? raw : prev_pct;
  }
  return raw < prev_pct ? raw : prev_pct;
}

std::uint8_t percent() {
  if (!sample_valid()) {
    return kPercentUnknown;
  }
  const std::uint8_t raw = percent_from_mv(millivolts());
  const bool chg = charging();
  // After unplug, drop the 99% charge clamp so a full cell can show 100.
  if (g_have_displayed && g_was_charging && !chg) {
    g_have_displayed = false;
  }
  g_was_charging = chg;
  g_displayed =
      apply_hysteresis(raw, g_displayed, chg, g_have_displayed);
  g_have_displayed = true;
  return g_displayed;
}

bool percent_valid() { return sample_valid(); }

bool charging() {
  if (g_hooks.is_charging == nullptr) {
    return false;
  }
  return g_hooks.is_charging(g_hooks.ctx);
}

bool sample_valid() {
  if (g_hooks.read_adc_raw == nullptr) {
    return false;
  }
  return g_hooks.read_adc_raw(g_hooks.ctx) >= 0;
}

}  // namespace battery
}  // namespace slate
