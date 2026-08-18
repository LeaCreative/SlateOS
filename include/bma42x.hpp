#pragma once

#include <cstddef>
#include <cstdint>

// BMA421 / BMA425 (I2C 0x18). Runtime chip detect; hardware step counter +
// any-motion IRQ for tilt-to-wake. Host injects the bus — no twi in tests.

namespace slate {
namespace bma {

enum class Chip : std::uint8_t {
  Unknown = 0,
  BMA421 = 1,  // chip id 0x11 (pre-Jul-2021 PineTime)
  BMA425 = 2,  // chip id 0x13
};

constexpr std::uint8_t kI2cAddr = 0x18u;
constexpr std::uint8_t kRegChipId = 0x00u;
constexpr std::uint8_t kChipId421 = 0x11u;
constexpr std::uint8_t kChipId425 = 0x13u;

struct Bus {
  // Write register `reg` with `data[len]`. Return true on ACK success.
  bool (*write_reg)(std::uint8_t reg, const std::uint8_t* data, std::size_t len,
                    void* ctx) = nullptr;
  bool (*read_reg)(std::uint8_t reg, std::uint8_t* data, std::size_t len,
                   void* ctx) = nullptr;
  void (*delay_ms)(std::uint32_t ms, void* ctx) = nullptr;
  void* ctx = nullptr;
};

class Driver {
public:
  void init(const Bus& bus);

  Chip chip() const { return chip_; }

  /**
   * INTERNAL_STATUS (0x2A) read after the last feature-config upload.
   *
   * 0x01 = ASIC booted. 0xFF means no upload has been attempted. Exposed
   * because it is the only honest answer to "did the pedometer come up".
   */
  std::uint8_t last_init_status() const { return last_init_status_; }

  /** True once the pedometer enable bit was written and read back verified. */
  bool step_counter_enabled() const { return step_enabled_; }
  bool ok() const { return chip_ != Chip::Unknown; }

  // Enable accel in low-power + attempt HW step counter.
  // If `feature_cfg` is null/empty, step reads stay 0 but any-motion still works.
  bool configure(const std::uint8_t* feature_cfg, std::size_t cfg_len,
                 std::uint8_t tilt_sensitivity_0_7);

  // Re-apply any-motion threshold (settings change).
  void set_tilt_sensitivity(std::uint8_t sens_0_7);

  std::uint32_t read_steps();
  void reset_steps();

  // True if INT status indicates any-motion / wrist activity since last clear.
  bool consume_tilt_irq();

  /**
   * Raw acceleration, milli-g-ish counts at the configured range.
   *
   * Basic data registers (0x12..0x17), live from power-on and independent of
   * the feature config blob — which is what makes raise-to-wake possible on a
   * sensor whose ASIC never loaded. Returns false if the read fails; leaves the
   * outputs untouched in that case so a caller can hold its last sample.
   *
   * Near-zero samples (no gravity) re-enable the accelerometer and drop
   * advanced power save for the retry. All-zero ACC_DATA with a live I2C ACK
   * is how raise/shake die while chip-id / INTERNAL_STATUS still look fine.
   */
  bool read_accel(std::int16_t* x, std::int16_t* y, std::int16_t* z);

  /** Last PWR_CTRL (0x7D) observed during [read_accel]. 0xFF = never read. */
  std::uint8_t last_power_ctrl() const { return last_power_ctrl_; }

private:
  bool wr(std::uint8_t reg, std::uint8_t v);
  bool rd(std::uint8_t reg, std::uint8_t* v);
  bool write_feature_config(const std::uint8_t* cfg, std::size_t len);
  bool enable_step_counter();
  bool enable_any_motion(std::uint8_t sens);

  Bus bus_{};
  Chip chip_ = Chip::Unknown;
  std::uint8_t last_init_status_ = 0xFFu;
  // Where the feature block lives in the sensor's ASIC address space. Bosch
  // reads these back after the config load rather than assuming an address.
  std::uint8_t feature_addr_lsb_ = 0u;
  std::uint8_t feature_addr_msb_ = 0u;
  bool step_enabled_ = false;
  std::uint8_t last_power_ctrl_ = 0xFFu;
};

}  // namespace bma
}  // namespace slate
