#include "cst816s.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "power.hpp"
#include "twi.hpp"

#include <cstdint>

static constexpr std::uint8_t  kI2cAddr   = 0x15u;
static constexpr std::uint32_t kPinRst    = 10u;
static constexpr std::uint32_t kPinIrq    = 28u;
// InfiniTime reads 6 bytes from register 1, not 63 from register 0. A 63-byte
// read is what our driver did, and every one of them failed (N-31).
static constexpr std::uint8_t  kReadStart = 1u;
static constexpr std::uint32_t kTouchRegs = 6u;

// Controller configuration, mirroring InfiniTime's Cst816S::Init().
static constexpr std::uint8_t kRegMotionMask = 0xECu;
static constexpr std::uint8_t kRegIrqCtl     = 0xFAu;
static constexpr std::uint8_t kRegAutoReset  = 0xFBu;
// EnConLR | EnDClick.
static constexpr std::uint8_t kMotionMask = 0x05u;
// EnTouch | EnChange | EnMotion — decides when the IRQ line pulses. Leaving
// this at its power-on default is why the pin behaved as a level, not a pulse.
static constexpr std::uint8_t kIrqCtl = 0x70u;

// Offsets within the 6-byte block read from register 1.
static constexpr std::size_t kOffGesture = 0u;
static constexpr std::size_t kOffFingers = 1u;
static constexpr std::size_t kOffXh      = 2u;
static constexpr std::size_t kOffXl      = 3u;
static constexpr std::size_t kOffYh      = 4u;
static constexpr std::size_t kOffYl      = 5u;

namespace {

volatile bool g_irq_latched = false;
// Raw GPIOTE PORT latches. Distinguishes "the controller never asserts IRQ"
// from "the IRQ fires but the I2C read fails" — the buses_wake() fix assumed
// the latter without checking (N-31).
volatile std::uint32_t g_irq_count = 0u;
// I2C reads that failed outright. Separates "the bus read failed" from "the
// read succeeded but the controller reported no touch" — the storm fix proved
// the IRQ fires, so the remaining fault is one of these two (N-31).
std::uint32_t g_read_fail = 0u;

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

void hard_reset() {
  gpio_output(kPinRst, false);
  board::busy_wait_ms(10u);
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kPinRst);
  board::busy_wait_ms(50u);
}

/** True while the touch IRQ line is asserted (active low). */
bool irq_asserted() {
  return (nrf::reg<std::uint32_t>(nrf::gpio::IN) & (1u << kPinIrq)) == 0u;
}

void clear_detect_latch() {
  // Re-arm SENSE for the *opposite* level, turning a level detector into an
  // edge detector.
  //
  // Re-arming SENSE_LOW unconditionally re-latched immediately whenever the
  // controller held IRQ low — DETECT is a level signal, so clearing LATCH
  // while the pin is still low fires again at once. Measured 7980 interrupts
  // in 69 s (~115/s) with no touch data behind any of them (N-31): the storm
  // starved the app loop and produced nothing. InfiniTime edge-triggers this
  // pin; this is the same behaviour built from SENSE, which is kept for the
  // power reasons noted below.
  const std::uint32_t want =
      irq_asserted() ? nrf::gpio::PIN_CNF_SENSE_HIGH : nrf::gpio::PIN_CNF_SENSE_LOW;

  std::uint32_t cnf = nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(kPinIrq));
  cnf = (cnf & ~(3u << 16)) | nrf::gpio::PIN_CNF_SENSE_DISABLED;
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(kPinIrq)) = cnf;
  nrf::reg<std::uint32_t>(nrf::gpio::LATCH) = (1u << kPinIrq);  // W1C
  cnf = (cnf & ~(3u << 16)) | want;
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(kPinIrq)) = cnf;
}

void configure_irq_pin_sense() {
  // Prefer PIN_CNF SENSE + GPIOTE PORT over edge-triggered IN events.
  // Edge GPIOTE has been measured up to ~0.47 mA in some configs (§8 / M14).
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(kPinIrq)) =
      nrf::gpio::PIN_CNF_DIR_INPUT |
      nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULLUP |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_LOW;

  // Disable any leftover channel EVENT config.
  nrf::reg<std::uint32_t>(nrf::gpiote::config(0u)) = nrf::gpiote::CONFIG_MODE_DISABLED;
  nrf::reg<std::uint32_t>(nrf::gpiote::EVENTS_PORT) = 0u;
  nrf::reg<std::uint32_t>(nrf::gpiote::INTENSET) = (1u << 31);  // PORT

  nrf::nvic_clear_pending(nrf::irq::GPIOTE);
  nrf::nvic_enable_irq(nrf::irq::GPIOTE);
  asm volatile("cpsie i" ::: "memory");
}

}  // namespace

extern "C" void GPIOTE_IRQHandler() {
  if (nrf::reg<std::uint32_t>(nrf::gpiote::EVENTS_PORT) != 0u) {
    nrf::reg<std::uint32_t>(nrf::gpiote::EVENTS_PORT) = 0u;
    // Only the asserting edge is a touch. The releasing edge just re-arms.
    if (irq_asserted()) {
      g_irq_latched = true;
      ++g_irq_count;
    }
    clear_detect_latch();
  }
  // Ignore legacy IN0 if somehow enabled.
  if (nrf::reg<std::uint32_t>(nrf::gpiote::events_in(0u)) != 0u) {
    nrf::reg<std::uint32_t>(nrf::gpiote::events_in(0u)) = 0u;
  }
}

namespace cst816s {

void init() {
  g_irq_latched = false;
  g_irq_count = 0u;
  g_read_fail = 0u;
  hard_reset();

  // Wake and configure the controller, mirroring InfiniTime. Slate previously
  // did neither: it reset the part, probed one byte, and left every control
  // register at its power-on default. The IRQ control register (0xFA) in
  // particular decides when the line pulses, so without it the pin sat
  // asserted and the GPIOTE PORT detector re-fired forever (N-31).
  std::uint8_t dummy = 0u;
  std::uint8_t reg = 0x15u;
  (void)twi::write_read(kI2cAddr, &reg, 1u, &dummy, 1u, 2000u);
  board::busy_wait_ms(5u);
  reg = 0xA7u;
  (void)twi::write_read(kI2cAddr, &reg, 1u, &dummy, 1u, 2000u);
  board::busy_wait_ms(5u);

  const std::uint8_t motion[2] = {kRegMotionMask, kMotionMask};
  (void)twi::write(kI2cAddr, motion, sizeof(motion), 2000u);
  const std::uint8_t irq[2] = {kRegIrqCtl, kIrqCtl};
  (void)twi::write(kI2cAddr, irq, sizeof(irq), 2000u);
  // Disable the 5 s auto-reset: it drops the live touch point mid-gesture.
  const std::uint8_t autorst[2] = {kRegAutoReset, 0x00u};
  (void)twi::write(kI2cAddr, autorst, sizeof(autorst), 2000u);

  configure_irq_pin_sense();
}

bool irq_pending() {
  return g_irq_latched;
}

std::uint32_t irq_count() { return g_irq_count; }

std::uint32_t read_fail_count() { return g_read_fail; }

TouchSample poll() {
  TouchSample sample{};

  if (g_irq_latched) {
    g_irq_latched = false;
  }

  // Full re-init, not just power::buses_wake().
  //
  // The controller lives on TWIM1, which buses_idle() disables on entry to
  // every power state except Active. buses_wake() only sets ENABLE — but
  // disabling TWIM returns SCL/SDA to GPIO control, so PSEL, FREQUENCY, SHORTS
  // and the pin pull-ups all need restoring before a transfer will work.
  // Measured 12 read failures against 12 interrupts with buses_wake() alone
  // (N-31): the IRQ was correct and every read went out over a dead bus.
  //
  // Cost is a disable/enable cycle per touch event, not per loop iteration.
  twi::init();

  alignas(4) std::uint8_t data[kTouchRegs] = {};
  static std::uint8_t start = kReadStart;
  const twi::Status st =
      twi::write_read(kI2cAddr, &start, 1u, data, kTouchRegs, 3000u);

  if (st != twi::Status::Ok) {
    ++g_read_fail;
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
