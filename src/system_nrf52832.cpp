#include "board.hpp"
#include "nrf52832_regs.hpp"

#include <cstdint>

namespace {

void nvmc_wait_ready() {
  while (nrf::reg<std::uint32_t>(nrf::nvmc::READY) == 0u) {
  }
}

void timer0_init_for_busy_wait() {
  nrf::reg<std::uint32_t>(nrf::timer0::TASKS_STOP) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer0::TASKS_CLEAR) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer0::MODE) = nrf::timer0::MODE_TIMER;
  nrf::reg<std::uint32_t>(nrf::timer0::BITMODE) = nrf::timer0::BITMODE_32;
  nrf::reg<std::uint32_t>(nrf::timer0::PRESCALER) = 4u;  // 16 MHz / 2^4 = 1 MHz
  nrf::reg<std::uint32_t>(nrf::timer0::TASKS_START) = 1u;
}

void gpio_init() {
  nrf::reg<std::uint32_t>(nrf::gpio::PIN_CNF_BASE + (board::kMotorPin * 4u)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT |
      nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;

  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << board::kMotorPin);
  nrf::reg<std::uint32_t>(nrf::gpio::DIRSET) = (1u << board::kMotorPin);
}

void configure_uicr_nfc_as_gpio() {
  const std::uint32_t nfcpins = nrf::reg<std::uint32_t>(nrf::uicr::NFCPINS);
  if (nfcpins == nrf::uicr::NFCPINS_GPIO) {
    return;
  }

  nvmc_wait_ready();
  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = nrf::nvmc::CONFIG_WEN;
  nvmc_wait_ready();

  nrf::reg<std::uint32_t>(nrf::uicr::NFCPINS) = nrf::uicr::NFCPINS_GPIO;
  nvmc_wait_ready();

  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = nrf::nvmc::CONFIG_REN;
  nvmc_wait_ready();
}

}  // namespace

extern "C" void SystemInit() {
  // Enable the on-chip DC/DC regulator.
  nrf::reg<std::uint32_t>(nrf::power::DCDCEN) = 1u;

  // Select the 32.768 kHz low-frequency crystal oscillator.
  nrf::reg<std::uint32_t>(nrf::clock::LFCLKSRC) = nrf::clock::LFCLK_SRC_XTAL;

  // Clear stale start event before starting the LFCLK.
  nrf::reg<std::uint32_t>(nrf::clock::EVENTS_LFCLKSTARTED) = 0u;

  // Start the LFCLK explicitly.
  nrf::reg<std::uint32_t>(nrf::clock::TASKS_LFCLKSTART) = 1u;

  // Wait until the oscillator reports stable.
  while (nrf::reg<std::uint32_t>(nrf::clock::EVENTS_LFCLKSTARTED) == 0u) {
  }

  // Program UICR so P0.09/P0.10 act as GPIO rather than NFC pins.
  configure_uicr_nfc_as_gpio();

  timer0_init_for_busy_wait();
  gpio_init();
}

namespace board {

std::uint32_t micros() {
  nrf::reg<std::uint32_t>(nrf::timer0::TASKS_CAPTURE0) = 1u;
  return nrf::reg<std::uint32_t>(nrf::timer0::CC0);
}

void busy_wait_us(std::uint32_t microseconds) {
  const std::uint32_t start_us = micros();
  const std::uint32_t deadline_us = start_us + microseconds;
  while (static_cast<std::int32_t>(micros() - deadline_us) < 0) {
  }
}

void busy_wait_ms(std::uint32_t milliseconds) {
  busy_wait_us(milliseconds * 1000u);
}

void motor_on() {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kMotorPin);
}

void motor_off() {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << kMotorPin);
}

void pulse_motor(std::uint32_t milliseconds) {
  motor_on();
  busy_wait_ms(milliseconds);
  motor_off();
}

}  // namespace board
