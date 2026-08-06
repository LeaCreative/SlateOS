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
// twi::Status of the most recent touch read. A bare failure count cannot tell
// "the peripheral never ran" (Timeout) from "the slave did not answer"
// (AddressNack); that ambiguity cost three build-and-flash cycles on N-31.
std::uint8_t g_last_twi_status = 0u;

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

/** InfiniTime's validity check: anything outside the documented set is junk. */
bool is_known_gesture(std::uint8_t g) {
  switch (g) {
    case 0x00u:  // None
    case 0x01u:  // SlideDown
    case 0x02u:  // SlideUp
    case 0x03u:  // SlideLeft
    case 0x04u:  // SlideRight
    case 0x05u:  // SingleTap
    case 0x0Bu:  // DoubleTap
    case 0x0Cu:  // LongPress
      return true;
    default:
      return false;
  }
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
  g_last_twi_status = 0u;
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

  // Deliberately non-const: EasyDMA can only read Data RAM, and a const local
  // array may be promoted to .rodata in flash at -Os. Non-const locals cannot.
  std::uint8_t motion[2] = {kRegMotionMask, kMotionMask};
  (void)twi::write(kI2cAddr, motion, sizeof(motion), 2000u);
  std::uint8_t irq[2] = {kRegIrqCtl, kIrqCtl};
  (void)twi::write(kI2cAddr, irq, sizeof(irq), 2000u);
  // Disable the 5 s auto-reset: it drops the live touch point mid-gesture.
  std::uint8_t autorst[2] = {kRegAutoReset, 0x00u};
  (void)twi::write(kI2cAddr, autorst, sizeof(autorst), 2000u);

  configure_irq_pin_sense();
}

bool irq_pending() {
  return g_irq_latched;
}

std::uint32_t irq_count() { return g_irq_count; }

std::uint32_t read_fail_count() { return g_read_fail; }

std::uint8_t last_twi_status() { return g_last_twi_status; }

TouchSample poll() {
  TouchSample sample{};

  if (g_irq_latched) {
    g_irq_latched = false;
  }

  // No bus re-init here any more. twi::write_read() wakes TWIM1 on entry and
  // sleeps it on exit (InfiniTime TwiMaster), and ENABLE=0 preserves PSEL,
  // FREQUENCY and PIN_CNF — so there was never anything for a re-init to
  // restore.
  //
  // The reason every read failed regardless was the TWIM SHORTS bit positions
  // (nrf52832_regs.hpp): write_read never armed LASTTX→STARTRX, so after the
  // register byte the peripheral simply stopped doing anything and the wait
  // timed out. Three separate theories were tried against this symptom before
  // that — bus wake, full re-init, pin drive — because the failure counter did
  // not say *how* it failed. It now reports the status code.
  alignas(4) std::uint8_t data[kTouchRegs] = {};
  static std::uint8_t start = kReadStart;
  const twi::Status st =
      twi::write_read(kI2cAddr, &start, 1u, data, kTouchRegs, 3000u);

  g_last_twi_status = static_cast<std::uint8_t>(st);
  if (st != twi::Status::Ok) {
    ++g_read_fail;
    return sample;
  }

  const std::uint8_t raw_gesture = data[kOffGesture];
  sample.fingers = static_cast<std::uint8_t>(data[kOffFingers] & 0x0Fu);
  sample.x = static_cast<std::uint16_t>(
      ((static_cast<std::uint16_t>(data[kOffXh] & 0x0Fu) << 8u) |
       data[kOffXl]));
  sample.y = static_cast<std::uint16_t>(
      ((static_cast<std::uint16_t>(data[kOffYh] & 0x0Fu) << 8u) |
       data[kOffYl]));

  // InfiniTime rejects the whole sample when the coordinates or the gesture
  // byte are out of range rather than clamping — a read that returns nonsense
  // is a failed read, and clamping turns it into a plausible tap at the edge
  // of the screen. Slate clamped.
  if (sample.x >= 240u || sample.y >= 240u || !is_known_gesture(raw_gesture)) {
    return sample;
  }

  sample.gesture = static_cast<Gesture>(raw_gesture);
  sample.touching = sample.fingers > 0u;
  sample.valid = true;
  return sample;
}

void sleep() {
  // InfiniTime Cst816S::Sleep(). Not wired into power::enter() — see the note
  // in cst816s.hpp: the panel has to stay in normal mode for a tap to wake the
  // watch, which is also what InfiniTime does whenever a touch wake-up mode is
  // enabled. Kept so I-17 (display sleep) has the mirrored primitive available.
  gpio_output(kPinRst, false);
  board::busy_wait_ms(5u);
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kPinRst);
  board::busy_wait_ms(50u);
  std::uint8_t sleep_cmd[2] = {0xA5u, 0x03u};
  (void)twi::write(kI2cAddr, sleep_cmd, sizeof(sleep_cmd), 2000u);
}

void wake() {
  // InfiniTime Cst816S::Wakeup() is Init() — reset, wake reads, control
  // registers. Blocks ~65 ms, so it belongs on a wake transition, never in the
  // poll path.
  init();
}

}  // namespace cst816s
