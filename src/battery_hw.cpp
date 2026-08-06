#include "battery.hpp"
#include "battery_hw.hpp"
#include "nrf52832_regs.hpp"
#include "power.hpp"

#include <cstdint>

// SAADC + charge pin. Unknown ADC must not look like a healthy battery.

namespace slate {
namespace battery_hw {
namespace {

// InfiniTime PinMap: Charging 12, PowerPresent 19. Both active low.
constexpr std::uint32_t kChargePin = 12u;
constexpr std::uint32_t kPowerPresentPin = 19u;
int g_last_adc = -1;

int read_adc(void*) { return g_last_adc; }

bool is_charging(void*) {
  const std::uint32_t in = nrf::reg<std::uint32_t>(nrf::gpio::IN);
  return (in & (1u << kChargePin)) == 0u;
}

bool is_power_present(void*) {
  const std::uint32_t in = nrf::reg<std::uint32_t>(nrf::gpio::IN);
  return (in & (1u << kPowerPresentPin)) == 0u;
}

void cfg_input_nopull(std::uint32_t pin) {
  // InfiniTime configures both as plain inputs with the pull DISABLED — the
  // charger drives them. Slate had a pull-up on the charge pin.
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_INPUT | nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED | nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
}

}  // namespace

void init() {
  cfg_input_nopull(kChargePin);
  cfg_input_nopull(kPowerPresentPin);

  battery::Hooks h;
  h.read_adc_raw = &read_adc;
  h.is_charging = &is_charging;
  h.is_power_present = &is_power_present;
  battery::init(h);

  // Sample before any DFU/OTA gate can run (InfiniTime measures early too).
  sample_now();
}

void set_adc_raw(int raw) { g_last_adc = raw; }

int last_adc_raw() { return g_last_adc; }

void sample_now() { set_adc_raw(power::sample_battery_adc()); }

}  // namespace battery_hw
}  // namespace slate
