#pragma once

#include <cstddef>
#include <cstdint>

// SpiBus — exclusive ownership of the shared SPI0 bus.
//
// The ST7789 display and the XT25F32B external flash share the same physical
// SPI bus (SCK=P0.02, MOSI=P0.03).  Only one CS may be asserted at a time.
// SpiBus enforces that at the driver level: a client calls acquire(), does its
// work, then calls release().  Attempting to start a transfer without holding
// the lock is a programming error caught by assertion in debug builds.
//
// There is no RTOS mutex here — M0 is bare-metal single-threaded.  When the
// RTOS is introduced the lock body becomes a FreeRTOS mutex take/give.
//
// DMA transfers use SPIM0 (EasyDMA, up to 255 bytes per MAXCNT word, but the
// nRF52832 SPIM supports a full 16-bit TXD.MAXCNT so transfers of up to 65535
// bytes are possible in a single DMA kick).

namespace spi {

// Initialise SPIM0: SCK=P0.02, MOSI=P0.03, MISO disconnected, 8 MHz, mode 3.
// Must be called once before any acquire()/transfer() sequence.
void init();

// Acquire exclusive bus ownership.  Blocks (spin) if the bus is already held.
// In the bare-metal single-threaded M0 build this never actually spins — any
// call while the bus is held is a programming error.
void acquire();

// Release ownership.  CS pin management is the caller's responsibility.
void release();

// Synchronous DMA transmit of `len` bytes from `data`.
// CS must already be driven low by the caller before this call.
// Blocks until SPIM reports END (EasyDMA transfer complete).
// `data` must remain valid and in RAM for the duration (EasyDMA cannot read flash).
void transmit(const std::uint8_t* data, std::size_t len);

// Full-duplex transfer for SPI NOR (XT25). TX `tx_len` bytes then clock
// `rx_len` dummy bytes into `rx` (or TX zeros while receiving).
// Either pointer may be null if its length is 0. Buffers must be in RAM.
void transfer(const std::uint8_t* tx, std::size_t tx_len,
              std::uint8_t* rx, std::size_t rx_len);

// Convenience: assert (low) / deassert (high) a GPIO CS pin.
void cs_assert(std::uint32_t pin);
void cs_deassert(std::uint32_t pin);

// Flash CS pin (P0.05) — shared bus with display CS P0.25.
constexpr std::uint32_t kPinCsFlash = 5u;
constexpr std::uint32_t kPinCsDisplay = 25u;
constexpr std::uint32_t kPinMiso = 4u;

// Set DC/RS line (display command vs. data).
void dc_command();
void dc_data();

bool is_acquired();

}  // namespace spi
