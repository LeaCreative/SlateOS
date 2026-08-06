#pragma once

#include <cstdint>

// CST816S capacitive touch controller.
//
// The chip SLEEPS when idle and NACKs / does not respond on I2C — that is
// normal, not a fault.  Reads that fail with AddressNack are treated as
// "asleep, no new data".
//
// sleep() writes the 0xA5 sleep command, which historically wedged the
// controller until the battery drained. InfiniTime issues it only when no
// touch wake-up mode is enabled, and always resets the part first; that reset
// is what recovers it. Slate wants tap-to-wake, so nothing calls sleep() yet —
// the panel stays in normal mode, exactly as InfiniTime leaves it when
// DoubleTap wake is on.

namespace cst816s {

enum class Gesture : std::uint8_t {
  None       = 0x00u,
  SlideDown  = 0x01u,
  SlideUp    = 0x02u,
  SlideLeft  = 0x03u,
  SlideRight = 0x04u,
  SingleTap  = 0x05u,
  DoubleTap  = 0x0Bu,
  LongPress  = 0x0Cu,
};

struct TouchSample {
  Gesture gesture = Gesture::None;
  std::uint8_t fingers = 0u;
  std::uint16_t x = 0u;
  std::uint16_t y = 0u;
  /** A finger is on the panel right now (InfiniTime TouchInfos::touching). */
  bool touching = false;
  /** The read succeeded and the data is in range — NOT "a touch happened".
   *  Mirrors InfiniTime TouchInfos::isValid. */
  bool valid = false;
};

// Hard-reset the controller, configure IRQ on P0.28 via PIN_CNF SENSE +
// GPIOTE PORT (not edge-triggered IN — saves ~0.47 mA in some configs).
// Requires twi::init() first.
void init();

// True if the IRQ line latched a falling edge since the last poll.
bool irq_pending();

/** Raw GPIOTE PORT latches seen since init. Diagnostics. */
std::uint32_t irq_count();

/** I2C reads that returned an error. Diagnostics. */
std::uint32_t read_fail_count();

/** twi::Status of the last read: 0 Ok, 1 Timeout, 2 AddrNack, 3 DataNack,
 *  4 BusError. Says *how* a read failed, not just that it did. */
std::uint8_t last_twi_status();

// Clear the GPIOTE event and read touch registers.
// Returns a sample with valid=false if the controller is asleep / NACK.
TouchSample poll();

// InfiniTime Cst816S::Sleep() / Wakeup(). wake() is a full re-init and blocks
// ~65 ms — call it on a wake transition, never from the poll path.
void sleep();
void wake();

}  // namespace cst816s
