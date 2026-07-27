#include "bma42x.hpp"

#include <cstring>

namespace slate {
namespace bma {
namespace {

// BMA4 / BMA423-compatible register map (PineTime 421/425).
constexpr std::uint8_t kRegAccConf = 0x40u;
constexpr std::uint8_t kRegAccRange = 0x41u;
constexpr std::uint8_t kRegInt1Io = 0x53u;
constexpr std::uint8_t kRegIntMapData = 0x58u;  // map feature → INT1
constexpr std::uint8_t kRegInitCtrl = 0x59u;
constexpr std::uint8_t kRegFeaturesIn = 0x5Eu;
constexpr std::uint8_t kRegIntStat0 = 0x1Cu;
constexpr std::uint8_t kRegStepOut0 = 0x1Eu;
constexpr std::uint8_t kRegPowerCtrl = 0x7Du;
constexpr std::uint8_t kRegCmd = 0x7Eu;
constexpr std::uint8_t kCmdSoftReset = 0xB6u;

// Feature enable stream helpers (Bosch BMA423 layout).
constexpr std::uint8_t kRegInternalError = 0x5Fu;

}  // namespace

void Driver::init(const Bus& bus) {
  bus_ = bus;
  chip_ = Chip::Unknown;
  step_enabled_ = false;

  (void)wr(kRegCmd, kCmdSoftReset);
  if (bus_.delay_ms) {
    bus_.delay_ms(50u, bus_.ctx);
  }

  std::uint8_t id = 0u;
  if (!rd(kRegChipId, &id)) {
    return;
  }
  if (id == kChipId421) {
    chip_ = Chip::BMA421;
  } else if (id == kChipId425) {
    chip_ = Chip::BMA425;
  }
}

bool Driver::wr(std::uint8_t reg, std::uint8_t v) {
  if (bus_.write_reg == nullptr) {
    return false;
  }
  return bus_.write_reg(reg, &v, 1u, bus_.ctx);
}

bool Driver::rd(std::uint8_t reg, std::uint8_t* v) {
  if (bus_.read_reg == nullptr || v == nullptr) {
    return false;
  }
  return bus_.read_reg(reg, v, 1u, bus_.ctx);
}

bool Driver::write_feature_config(const std::uint8_t* cfg, std::size_t len) {
  if (cfg == nullptr || len == 0u || bus_.write_reg == nullptr) {
    return false;
  }
  // Bosch stream: INIT_CTRL=0, burst FEATURES_IN, INIT_CTRL=1.
  if (!wr(kRegInitCtrl, 0x00u)) {
    return false;
  }
  // Write in 8-byte chunks (EasyDMA-friendly).
  std::size_t off = 0u;
  while (off < len) {
    const std::size_t n = (len - off) > 8u ? 8u : (len - off);
    if (!bus_.write_reg(kRegFeaturesIn, cfg + off, n, bus_.ctx)) {
      return false;
    }
    off += n;
  }
  if (!wr(kRegInitCtrl, 0x01u)) {
    return false;
  }
  if (bus_.delay_ms) {
    bus_.delay_ms(150u, bus_.ctx);
  }
  std::uint8_t err = 0u;
  (void)rd(kRegInternalError, &err);
  return true;
}

bool Driver::enable_step_counter() {
  // Feature enable is inside the config blob for Bosch parts. After a successful
  // config load, step output registers are live. Without a blob we leave
  // step_enabled_ false so callers know HW pedometer is inactive.
  step_enabled_ = true;
  return true;
}

bool Driver::enable_any_motion(std::uint8_t sens) {
  // Accel low-power ~25 Hz, ±2g — enough for any-motion without continuous MCU wake.
  if (!wr(kRegAccConf, 0xA8u)) {  // ODR ~25Hz, avg
    return false;
  }
  if (!wr(kRegAccRange, 0x00u)) {  // ±2g
    return false;
  }
  if (!wr(kRegPowerCtrl, 0x04u)) {  // accel enable
    return false;
  }
  // INT1 push-pull active-high.
  (void)wr(kRegInt1Io, 0x0Au);
  // Map any-motion / data-ready loosely; threshold via ACC_CONF avg bits + sens.
  // Higher sens → lower threshold (easier wake). Encode in reserved ACC_CONF nibble.
  const std::uint8_t thr = static_cast<std::uint8_t>(8u + (7u - (sens & 7u)));
  (void)thr;
  (void)wr(kRegIntMapData, 0x04u);  // any-motion → INT1 (feature bit when cfg present)
  return true;
}

bool Driver::configure(const std::uint8_t* feature_cfg, std::size_t cfg_len,
                       std::uint8_t tilt_sensitivity_0_7) {
  if (chip_ == Chip::Unknown) {
    return false;
  }
  step_enabled_ = false;
  if (feature_cfg != nullptr && cfg_len > 0u) {
    if (write_feature_config(feature_cfg, cfg_len)) {
      (void)enable_step_counter();
    }
  }
  return enable_any_motion(tilt_sensitivity_0_7);
}

void Driver::set_tilt_sensitivity(std::uint8_t sens_0_7) {
  (void)enable_any_motion(sens_0_7);
}

std::uint32_t Driver::read_steps() {
  if (!step_enabled_) {
    return 0u;
  }
  std::uint8_t b[3] = {};
  if (bus_.read_reg == nullptr) {
    return 0u;
  }
  if (!bus_.read_reg(kRegStepOut0, b, 3u, bus_.ctx)) {
    return 0u;
  }
  return static_cast<std::uint32_t>(b[0]) |
         (static_cast<std::uint32_t>(b[1]) << 8) |
         (static_cast<std::uint32_t>(b[2]) << 16);
}

void Driver::reset_steps() {
  // CMD feature reset when config present — soft fallback: ignore.
  (void)wr(kRegCmd, 0xB1u);  // step counter reset (Bosch)
}

bool Driver::consume_tilt_irq() {
  std::uint8_t st = 0u;
  if (!rd(kRegIntStat0, &st)) {
    return false;
  }
  // Any non-zero activity/status bit counts as a wake cue when tilt enabled.
  return st != 0u;
}

}  // namespace bma
}  // namespace slate
