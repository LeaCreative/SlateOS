#include "battery.hpp"
#include "battery_hw.hpp"
#include "nrf52832_regs.hpp"
#include "power.hpp"

#include <cstdint>

// SAADC + charge pin. Unknown ADC must not look like a healthy battery.

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

  // Sample before any DFU/OTA gate can run (InfiniTime measures early too).
  sample_now();
}

void set_adc_raw(int raw) { g_last_adc = raw; }

int last_adc_raw() { return g_last_adc; }

void sample_now() { set_adc_raw(power::sample_battery_adc()); }

}  // namespace battery_hw
}  // namespace slate
