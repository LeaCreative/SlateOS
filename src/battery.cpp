#include "battery.hpp"

namespace slate {
namespace battery {
namespace {
Hooks g_hooks{};
}  // namespace

void init(const Hooks& hooks) { g_hooks = hooks; }

std::uint16_t millivolts() {
  if (g_hooks.read_adc_raw == nullptr) {
    return 4000u;
  }
  const int adc = g_hooks.read_adc_raw(g_hooks.ctx);
  if (adc < 0) {
    return 4000u;
  }
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(adc) * 2000u) /
                                    1241u);
}

std::uint8_t percent() {
  const std::uint16_t mv = millivolts();
  // Rough LiPo map 3.30V–4.20V.
  if (mv <= 3300u) {
    return 0u;
  }
  if (mv >= 4200u) {
    return 100u;
  }
  return static_cast<std::uint8_t>(((mv - 3300u) * 100u) / 900u);
}

bool charging() {
  if (g_hooks.is_charging == nullptr) {
    return false;
  }
  return g_hooks.is_charging(g_hooks.ctx);
}

}  // namespace battery
}  // namespace slate
