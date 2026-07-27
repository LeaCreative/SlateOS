#include "battery.hpp"
#include "nrf52832_regs.hpp"

#include <cstdint>

// SAADC + charge pin. Until full SAADC bring-up, millivolts fall back to 4.0 V
// when the ADC is not yet sampled; charge detect uses P0.12 (low = charging).

namespace slate {
namespace battery_hw {
namespace {

constexpr std::uint32_t kChargePin = 12u;
int g_last_adc = -1;

int read_adc(void*) { return g_last_adc; }

bool is_charging(void*) {
  const std::uint32_t in = nrf::reg<std::uint32_t>(nrf::gpio::IN);
  return (in & (1u << kChargePin)) == 0u;
}

}  // namespace

void init() {
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(kChargePin)) =
      nrf::gpio::PIN_CNF_DIR_INPUT | nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULLUP | nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;

  battery::Hooks h;
  h.read_adc_raw = &read_adc;
  h.is_charging = &is_charging;
  battery::init(h);
}

// Optional: push a raw SAADC sample from a future sensors task.
void set_adc_raw(int raw) { g_last_adc = raw; }

}  // namespace battery_hw
}  // namespace slate
