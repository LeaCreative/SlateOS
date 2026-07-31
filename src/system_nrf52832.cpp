#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "wdt.hpp"

#include <cstdint>

namespace {

void nvmc_wait_ready() {
  while (nrf::reg<std::uint32_t>(nrf::nvmc::READY) == 0u) {
  }
}

// TIMER0 is reserved for NimBLE's nRF52 link layer (RADIO + RTC0 + TIMER0).
// Slate's micros() / busy-wait timebase uses TIMER1 only.
void timer1_init_for_busy_wait() {
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_STOP) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_CLEAR) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::MODE) = nrf::timer1::MODE_TIMER;
  nrf::reg<std::uint32_t>(nrf::timer1::BITMODE) = nrf::timer1::BITMODE_32;
  nrf::reg<std::uint32_t>(nrf::timer1::PRESCALER) = 4u;  // 16 MHz / 2^4 = 1 MHz
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_START) = 1u;
}

void gpio_motor_force_off() {
  // PineTime haptic is active-LOW (InfiniTime: pin_set=off, pin_clear=on).
  // Drive P0.16 HIGH as output ASAP — floating or driven-low = continuous buzz.
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << board::kMotorPin);
  nrf::reg<std::uint32_t>(nrf::gpio::DIRSET) = (1u << board::kMotorPin);
  nrf::reg<std::uint32_t>(nrf::gpio::PIN_CNF_BASE + (board::kMotorPin * 4u)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT |
      nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << board::kMotorPin);
}

void gpio_init() {
  gpio_motor_force_off();
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
  // Motor off before LFCLK / UICR / anything that can hang or fault.
  gpio_motor_force_off();

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

  timer1_init_for_busy_wait();
  gpio_init();
}

namespace board {

std::uint32_t micros() {
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_CAPTURE0) = 1u;
  return nrf::reg<std::uint32_t>(nrf::timer1::CC0);
}

void busy_wait_us(std::uint32_t microseconds) {
  const std::uint32_t start_us = micros();
  const std::uint32_t deadline_us = start_us + microseconds;
  while (static_cast<std::int32_t>(micros() - deadline_us) < 0) {
  }
}

void busy_wait_ms(std::uint32_t milliseconds) {
  // InfiniTime MCUBoot arms WDT before app entry; long init delays must pet it.
  // Use pet_service so a held side button can still force reset (InfiniTime).
  while (milliseconds > 0u) {
    const std::uint32_t chunk = (milliseconds > 500u) ? 500u : milliseconds;
    busy_wait_us(chunk * 1000u);
    milliseconds -= chunk;
    slate::wdt::pet_service();
  }
}

void motor_on() {
  // Active-low enable.
  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << kMotorPin);
}

void motor_off() {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kMotorPin);
}

void pulse_motor(std::uint32_t milliseconds) {
  motor_on();
  busy_wait_ms(milliseconds);
  motor_off();
}

[[noreturn]] void system_reset() {
  constexpr std::uintptr_t kAircr = 0xE000ED0Cu;
  *reinterpret_cast<volatile std::uint32_t*>(kAircr) =
      (0x05FAu << 16) | (1u << 2);
  while (true) {
  }
}

namespace {

bool reboot_button_pressed() {
  // Enable is left high (InfiniTime); pressed = sense high.
  return (nrf::reg<std::uint32_t>(nrf::gpio::IN) & (1u << kButtonSensePin)) != 0u;
}

std::uint32_t g_reset_reason = 0u;

}  // namespace

void button_hw_init() {
  nrf::reg<std::uint32_t>(nrf::gpio::PIN_CNF_BASE + (kButtonEnablePin * 4u)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT |
      nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
  nrf::reg<std::uint32_t>(nrf::gpio::PIN_CNF_BASE + (kButtonSensePin * 4u)) =
      nrf::gpio::PIN_CNF_DIR_INPUT |
      nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULLDOWN |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
  nrf::reg<std::uint32_t>(nrf::gpio::DIRSET) = (1u << kButtonEnablePin);
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kButtonEnablePin);
}

void capture_reset_reason() {
  constexpr std::uintptr_t kResetReas = 0x40000400u;
  g_reset_reason = nrf::reg<std::uint32_t>(kResetReas);
  // Write-1-to-clear: the register otherwise ORs every reset cause together and
  // the next boot cannot tell which one just happened.
  nrf::reg<std::uint32_t>(kResetReas) = g_reset_reason;
}

std::uint32_t reset_reason() { return g_reset_reason; }

bool button_raw() { return reboot_button_pressed(); }

void poll_reboot_button() {
  // Arming requires a confirmed release, and a press has to survive several
  // consecutive reads. A single spurious high must never be able to reset the
  // watch: after the fact a reset from here is indistinguishable from a crash,
  // because RESETREAS reports SREQ either way.
  static bool armed = false;
  static bool was_down = false;
  static std::uint32_t down_since_us = 0u;
  static std::uint8_t down_streak = 0u;
  constexpr std::uint8_t kDebounceReads = 8u;
  constexpr std::uint32_t kHoldUs = 8000000u;  // InfiniTime-like ~8 s

  const bool down = reboot_button_pressed();
  const std::uint32_t now = micros();
  if (!down) {
    armed = true;
    was_down = false;
    down_streak = 0u;
    return;
  }
  if (!armed) {
    return;
  }
  if (down_streak < kDebounceReads) {
    ++down_streak;
    return;
  }
  if (!was_down) {
    down_since_us = now;
    was_down = true;
    return;
  }
  if (static_cast<std::uint32_t>(now - down_since_us) >= kHoldUs) {
    system_reset();
  }
}

}  // namespace board
