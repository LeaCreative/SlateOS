#pragma once

#include <cstddef>
#include <cstdint>

// SpiBus — exclusive ownership of the shared SPI0 bus.
//
// The ST7789 display and the XT25F32B external flash share the same physical
// SPI bus (SCK=P0.02, MOSI=P0.03).  Only one CS may be asserted at a time.
// With FreeRTOS, acquire()/release() take a mutex (InfiniTime SpiMaster pattern).
// Nested acquire from the same task is a programming error.
//
// nRF52832 SPIM TXD.MAXCNT is 8-bit (max 255). transmit()/transfer() chunk.
// EVENTS_END waits are bounded (kEndTimeoutUs); timeout recovers SPIM and
// returns false so callers can fail cleanly (N-5 / InfiniTime hygiene).
// Full IRQ-driven SpiMaster mirror is deferred — see docs/infinitime-parity.md.

namespace spi {

constexpr std::uint32_t kPinCsFlash = 5u;
constexpr std::uint32_t kPinCsDisplay = 25u;
constexpr std::uint32_t kPinMiso = 4u;

// 255 B @ 8 MHz ≈ 255 µs; 5 ms is a generous per-chunk cap.
constexpr std::uint32_t kEndTimeoutUs = 5000u;

struct Stats {
  std::uint32_t end_timeouts = 0u;
  std::uint32_t protocol_faults = 0u;  // nested acquire, missing hold, dual CS
};

void init();
void acquire();
void release();

// false on timeout / not acquired. On timeout: SPIM recovered, both CS high.
bool transmit(const std::uint8_t* data, std::size_t len);
bool transfer(const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
              std::size_t rx_len);

void cs_assert(std::uint32_t pin);
void cs_deassert(std::uint32_t pin);

void dc_command();
void dc_data();

bool is_acquired();
Stats stats();
void reset_stats();

// Host / unit: µs budget for a chunk of `bytes` at 8 MHz (clamped to timeout).
inline std::uint32_t end_wait_budget_us(std::size_t bytes) {
  // 8 MHz → ~1 µs/byte; 4× margin + 200 µs floor, clamp to kEndTimeoutUs.
  const std::uint32_t raw =
      static_cast<std::uint32_t>(bytes == 0u ? 1u : bytes) * 4u + 200u;
  return raw > kEndTimeoutUs ? kEndTimeoutUs : raw;
}

}  // namespace spi
