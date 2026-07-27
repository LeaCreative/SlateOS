#pragma once

#include <cstdint>

// CST816S capacitive touch controller.
//
// The chip SLEEPS when idle and NACKs / does not respond on I2C — that is
// normal, not a fault.  Reads that fail with AddressNack are treated as
// "asleep, no new data".
//
// Do NOT write the sleep command: historically that wedged the controller
// until the battery drained.

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
  bool valid = false;
};

// Hard-reset the controller, configure IRQ on P0.28 (GPIOTE), enable NVIC.
// Requires twi::init() first.
void init();

// True if the IRQ line latched a falling edge since the last poll.
bool irq_pending();

// Clear the GPIOTE event and read touch registers.
// Returns a sample with valid=false if the controller is asleep / NACK.
TouchSample poll();

}  // namespace cst816s
