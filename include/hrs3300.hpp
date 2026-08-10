#pragma once

#include <cstddef>
#include <cstdint>

// HRS3300 PPG (I2C 0x44). Register sequences match InfiniTime Hrs3300.
// Bus is injected so host tests mock TWI without the driver.

namespace slate {
namespace hrs {

constexpr std::uint8_t kI2cAddr = 0x44u;
/** PineTime HRS INT / test net — InfiniTime configures as input nopull. */
constexpr std::uint8_t kIntPin = 30u;

enum class Reg : std::uint8_t {
  Id = 0x00u,
  Enable = 0x01u,
  C1dataM = 0x08u,
  C0DataM = 0x09u,
  C0DataH = 0x0Au,
  PDriver = 0x0Cu,
  C1dataH = 0x0Du,
  C1dataL = 0x0Eu,
  C0dataL = 0x0Fu,
  Res = 0x16u,
  Hgain = 0x17u,
};

/** Steady 12.5 mA LED drive (InfiniTime ledDriveCurrentValue). */
constexpr std::uint8_t kLedDrive = 0x2Fu;

struct PackedHrsAls {
  std::uint16_t hrs = 0u;
  std::uint16_t als = 0u;
};

struct Bus {
  bool (*write_reg)(std::uint8_t reg, const std::uint8_t* data, std::size_t len,
                    void* ctx) = nullptr;
  bool (*read_reg)(std::uint8_t reg, std::uint8_t* data, std::size_t len,
                   void* ctx) = nullptr;
  void (*delay_ms)(std::uint32_t ms, void* ctx) = nullptr;
  /** Optional: configure P0.30 as input nopull (device only). */
  void (*cfg_int_pin)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

class Driver {
public:
  void init(const Bus& bus);
  void enable();
  void disable();
  PackedHrsAls read_hrs_als();

  bool enabled() const { return enabled_; }

private:
  bool wr(std::uint8_t reg, std::uint8_t v);
  bool rd(std::uint8_t reg, std::uint8_t* v);

  Bus bus_{};
  bool enabled_ = false;
};

}  // namespace hrs
}  // namespace slate
