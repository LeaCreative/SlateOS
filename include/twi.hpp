#pragma once

#include <cstddef>
#include <cstdint>

// TWI (I2C) master wrapper on TWIM1.
//
// SPIM0 already owns peripheral instance 0 (0x40003000).  Touch/accel/HR all
// share the I2C bus on P0.06/P0.07, so they use TWIM1 (0x40004000).
//
// Every transfer has a hard timeout.  On timeout or ERROR the driver runs bus
// recovery: bit-bang SCL until SDA releases, then generate a STOP.

namespace twi {

enum class Status : std::uint8_t {
  Ok = 0,
  Timeout,
  AddressNack,
  DataNack,
  BusError,
};

void init();

// InfiniTime TwiMaster::Sleep() / Wakeup(): ENABLE alone. PSEL, FREQUENCY and
// the pin configuration all survive, so a woken bus needs no re-init. Every
// transfer below already wakes on entry and sleeps on exit — these exist for
// callers that want the bus down explicitly (power::buses_idle).
void sleep();
void wake();

// Write `len` bytes to `addr` (7-bit).  Returns Status::Ok on success.
Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len,
             std::uint32_t timeout_us = 5000u);

// Read `len` bytes from `addr`.
Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len,
            std::uint32_t timeout_us = 5000u);

// Write a register address, then read `len` bytes (repeated-start).
Status write_read(std::uint8_t addr,
                  const std::uint8_t* tx, std::size_t tx_len,
                  std::uint8_t* rx, std::size_t rx_len,
                  std::uint32_t timeout_us = 5000u);

// Clock out stuck slaves and force a STOP condition on the bus.
void recover_bus();

/**
 * Status of the most recent transfer.
 *
 * A failure count alone cannot tell a bus-level fault from a peripheral that
 * never ran: the SHORTS defect made every transfer end in Timeout, which reads
 * identically to AddressNack in a counter. Surfaced on diag line 3 so the next
 * failure names itself.
 */
Status last_status();

}  // namespace twi
