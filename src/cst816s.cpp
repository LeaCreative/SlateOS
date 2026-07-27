#include "cst816s.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "twi.hpp"

#include <cstdint>

static constexpr std::uint8_t  kI2cAddr   = 0x15u;
static constexpr std::uint32_t kPinRst    = 10u;
static constexpr std::uint32_t kPinIrq    = 28u;
static constexpr std::uint32_t kGpioteCh  = 0u;
static constexpr std::uint32_t kTouchRegs = 63u;

// Touch blob layout when reading from register 0 (first 63 registers):
//   byte 1 = gesture ID (authoritative — see CLAUDE.md / roadmap M2)
//   byte 2 = finger count (low nibble)
//   bytes 3..6 = XH, XL, YH, YL  (12-bit coords in low 12 bits of each pair)
static constexpr std::size_t kOffGesture = 1u;
static constexpr std::size_t kOffFingers = 2u;
static constexpr std::size_t kOffXh      = 3u;
static constexpr std::size_t kOffXl      = 4u;
static constexpr std::size_t kOffYh      = 5u;
static constexpr std::size_t kOffYl      = 6u;

namespace {

volatile bool g_irq_latched = false;

void gpio_output(std::uint32_t pin, bool high) {
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT |
      nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
  if (high) {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << pin);
  } else {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << pin);
  }
}

void gpio_input_pullup(std::uint32_t pin) {
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_INPUT |
      nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULLUP |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
}

void hard_reset() {
  gpio_output(kPinRst, false);
  board::busy_wait_ms(10u);
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kPinRst);
  board::busy_wait_ms(50u);
}

void configure_irq() {
  gpio_input_pullup(kPinIrq);

  // GPIOTE channel 0: event on falling edge of IRQ (touch asserts low).
  nrf::reg<std::uint32_t>(nrf::gpiote::config(kGpioteCh)) =
      nrf::gpiote::CONFIG_MODE_EVENT |
      (kPinIrq << 8u) |
      nrf::gpiote::CONFIG_POLARITY_HITOLO;

  nrf::reg<std::uint32_t>(nrf::gpiote::events_in(kGpioteCh)) = 0u;
  nrf::reg<std::uint32_t>(nrf::gpiote::INTENSET) = (1u << kGpioteCh);

  nrf::nvic_clear_pending(nrf::irq::GPIOTE);
  nrf::nvic_enable_irq(nrf::irq::GPIOTE);
  asm volatile("cpsie i" ::: "memory");
}

}  // namespace

extern "C" void GPIOTE_IRQHandler() {
  if (nrf::reg<std::uint32_t>(nrf::gpiote::events_in(kGpioteCh)) != 0u) {
    nrf::reg<std::uint32_t>(nrf::gpiote::events_in(kGpioteCh)) = 0u;
    g_irq_latched = true;
  }
}

namespace cst816s {

void init() {
  g_irq_latched = false;
  hard_reset();
  configure_irq();
  // Probe once — NACK here is fine (controller may already be asleep).
  std::uint8_t probe = 0u;
  std::uint8_t reg0 = 0u;
  (void)twi::write_read(kI2cAddr, &reg0, 1u, &probe, 1u, 2000u);
}

bool irq_pending() {
  return g_irq_latched;
}

TouchSample poll() {
  TouchSample sample{};

  if (g_irq_latched) {
    g_irq_latched = false;
  }

  alignas(4) std::uint8_t data[kTouchRegs] = {};
  // EasyDMA TX source must live in RAM for the whole transfer.
  static std::uint8_t reg0 = 0u;
  const twi::Status st =
      twi::write_read(kI2cAddr, &reg0, 1u, data, kTouchRegs, 3000u);

  if (st == twi::Status::AddressNack) {
    // Controller asleep — normal.  No new sample.
    return sample;
  }
  if (st != twi::Status::Ok) {
    // Bus glitch — recovered inside twi::; treat as no sample.
    return sample;
  }

  sample.gesture = static_cast<Gesture>(data[kOffGesture]);
  sample.fingers = static_cast<std::uint8_t>(data[kOffFingers] & 0x0Fu);
  sample.x = static_cast<std::uint16_t>(
      ((static_cast<std::uint16_t>(data[kOffXh] & 0x0Fu) << 8u) |
       data[kOffXl]));
  sample.y = static_cast<std::uint16_t>(
      ((static_cast<std::uint16_t>(data[kOffYh] & 0x0Fu) << 8u) |
       data[kOffYl]));

  if (sample.x >= 240u) {
    sample.x = 239u;
  }
  if (sample.y >= 240u) {
    sample.y = 239u;
  }

  sample.valid = (sample.gesture != Gesture::None) || (sample.fingers > 0u);
  return sample;
}

}  // namespace cst816s
