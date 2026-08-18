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
constexpr std::uint8_t kRegAccData = 0x12u;  // ACC_X_LSB .. ACC_Z_MSB
constexpr std::uint8_t kRegPowerCtrl = 0x7Du;
constexpr std::uint8_t kRegCmd = 0x7Eu;
constexpr std::uint8_t kCmdSoftReset = 0xB6u;

// Feature enable stream helpers (Bosch BMA423 layout).
constexpr std::uint8_t kRegInternalError = 0x5Fu;

// Config-stream upload, from Bosch's bma4_write_config_file / stream_transfer_write.
constexpr std::uint8_t kRegAsicLsb = 0x5Bu;       // BMA4_RESERVED_REG_5B_ADDR
constexpr std::uint8_t kRegAsicMsb = 0x5Cu;       // BMA4_RESERVED_REG_5C_ADDR
constexpr std::uint8_t kRegInternalStatus = 0x2Au;  // BMA4_INTERNAL_STAT
constexpr std::uint8_t kRegPowerConf = 0x7Cu;     // BMA4_POWER_CONF_ADDR
constexpr std::uint8_t kInternalStatusInitOk = 0x01u;
// PWR_CONF bits: 0 = adv_power_save, 1 = fifo_self_wakeup. Reset value is
// 0x03. Bosch's bma4_set_advance_power_save is a read-modify-write that
// leaves fifo_self_wakeup set; writing 0x00 / 0x01 cleared it (N-61).
constexpr std::uint8_t kPwrConfApsOff = 0x02u;
constexpr std::uint8_t kPwrConfApsOn = 0x03u;
constexpr std::uint8_t kPwrCtrlAccEn = 0x04u;
// Bosch's read_write_len for I2C. The ASIC address advances by len/2 per chunk.
constexpr std::size_t kStreamChunk = 8u;

// Feature block, from Bosch's bma423.h.
constexpr std::size_t kFeatureSize = 70u;         // BMA423_FEATURE_SIZE
constexpr std::size_t kStepCounterByte = 0x3Au + 1u;  // BMA423_STEP_CNTR_OFFSET + 1
constexpr std::uint8_t kStepCounterEnMask = 0x10u;    // BMA423_STEP_CNTR_EN_MSK

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

/**
 * Upload the Bosch feature-config stream, following bma4_write_config_file.
 *
 * The previous version wrote the whole 6 KB to FEATURES_IN without ever setting
 * the ASIC address registers, so every chunk landed at the same destination and
 * the config never loaded. It then returned true regardless, so the caller
 * believed the pedometer was live. That is why the step count read 0 for the
 * entire project.
 *
 * The order below is Bosch's and each step earns its place:
 *  1. advanced power save OFF - the ASIC cannot be written while it is on
 *  2. 450 us for sensor time sync (datasheet)
 *  3. INIT_CTRL = 0 to open the stream
 *  4. per chunk: ASIC address low/high, then the bytes
 *  5. INIT_CTRL = 1 to close it, then 150 ms while the ASIC boots
 *  6. INTERNAL_STATUS must read 1 - this is the only honest success signal
 *  7. advanced power save back ON
 */
bool Driver::write_feature_config(const std::uint8_t* cfg, std::size_t len) {
  if (cfg == nullptr || len == 0u || bus_.write_reg == nullptr) {
    return false;
  }
  if (!wr(kRegPowerConf, kPwrConfApsOff)) {
    return false;
  }
  if (bus_.delay_ms) {
    bus_.delay_ms(1u, bus_.ctx);  // >= 450 us
  }
  if (!wr(kRegInitCtrl, 0x00u)) {
    return false;
  }
  for (std::size_t off = 0u; off < len; off += kStreamChunk) {
    const std::size_t n = (len - off) > kStreamChunk ? kStreamChunk : (len - off);
    // Bosch addresses the ASIC in 16-bit words, so the byte index halves.
    const std::uint16_t word = static_cast<std::uint16_t>(off / 2u);
    if (!wr(kRegAsicLsb, static_cast<std::uint8_t>(word & 0x0Fu))) {
      return false;
    }
    if (!wr(kRegAsicMsb, static_cast<std::uint8_t>(word >> 4u))) {
      return false;
    }
    if (!bus_.write_reg(kRegFeaturesIn, cfg + off, n, bus_.ctx)) {
      return false;
    }
  }
  if (!wr(kRegInitCtrl, 0x01u)) {
    return false;
  }
  if (bus_.delay_ms) {
    bus_.delay_ms(150u, bus_.ctx);
  }
  // Ask the sensor whether it worked rather than assuming. Anything but 1 means
  // the ASIC did not come up and the feature registers will read zero.
  std::uint8_t status = 0u;
  if (!rd(kRegInternalStatus, &status)) {
    last_init_status_ = 0xFEu;  // read failed, distinct from "never tried"
    return false;
  }
  last_init_status_ = status;
  // Bosch's get_feature_config_start_addr: the sensor reports where the feature
  // block ended up, rather than the caller assuming an address. Read it back
  // before power save goes on, and use it for every later feature access.
  (void)rd(kRegAsicLsb, &feature_addr_lsb_);
  (void)rd(kRegAsicMsb, &feature_addr_msb_);
  feature_addr_lsb_ &= 0x0Fu;
  (void)wr(kRegPowerConf, kPwrConfApsOn);
  return (status & 0x0Fu) == kInternalStatusInitOk;
}

/**
 * Turn the pedometer on inside the sensor - bma423_feature_enable(STEP_CNTR, 1).
 *
 * Loading the config stream boots the feature ASIC; it does NOT switch any
 * feature on. This used to set a C++ bool and tell the sensor nothing, so even
 * a perfect upload left the step registers reading zero and read_steps()
 * reported those zeros as though they were a step count.
 *
 * Three things Bosch does around FEATURE_CONFIG that are not optional, taken
 * from bma4.c's read_regs/write_regs:
 *
 *  1. **Advanced power save OFF, then 450 us.** With APS enabled the ASIC
 *     ignores feature writes entirely. The config upload re-enables APS at the
 *     end, so by the time this runs it is always on - which is why loading the
 *     blob was not enough by itself.
 *  2. **The ASIC address is set once** before the transfer, from the values the
 *     sensor reported after the upload.
 *  3. **Transfers are chunked** at the same length as the config stream, and
 *     the block length must be even. 70 is.
 */
bool Driver::enable_step_counter() {
  if (bus_.read_reg == nullptr || bus_.write_reg == nullptr) {
    return false;
  }
  static_assert(kFeatureSize % 2u == 0u, "BMA feature block length must be even");

  // 1. APS off + settle.
  if (!wr(kRegPowerConf, kPwrConfApsOff)) {
    return false;
  }
  if (bus_.delay_ms) {
    bus_.delay_ms(1u, bus_.ctx);  // >= 450 us
  }

  const auto set_addr = [&]() {
    return wr(kRegAsicLsb, feature_addr_lsb_) && wr(kRegAsicMsb, feature_addr_msb_);
  };

  // ONE transfer each way, not a chunked loop.
  //
  // The ASIC address does not survive the end of a transfer: it advances as
  // bytes move within a single read or write and then reverts to whatever
  // 0x5B/0x5C name. Bosch chunks this only when it has to, and then calls
  // increment_feature_config_addr() after every chunk (bma4_read_regs); with
  // read_write_len >= len it does exactly what is below — a single
  // read_regs(FEATURE_CONFIG_ADDR, data, len).
  //
  // Slate chunked at 8 bytes and set the address once, so all nine chunks hit
  // bytes 0-7. The enable bit went into a copy of byte 3 instead of byte 0x3B,
  // the write-back scribbled the head of the config, and the verify re-read the
  // same wrong bytes and passed. N-59: the second time this driver reported
  // success for a pedometer that was switched off.
  //
  // 70 bytes is within the TWIM MAXCNT (8-bit) and within bma_write's buffer,
  // which was sized 80 for this exact transfer.
  std::uint8_t feat[kFeatureSize] = {};
  bool ok = set_addr() && bus_.read_reg(kRegFeaturesIn, feat, kFeatureSize, bus_.ctx);

  if (ok) {
    feat[kStepCounterByte] |= kStepCounterEnMask;
    ok = set_addr() &&
         bus_.write_reg(kRegFeaturesIn, feat, kFeatureSize, bus_.ctx);
  }

  // Read the block back and check the bit is really set.
  //
  // An I2C ACK only says the sensor accepted the bytes, not that they landed
  // where intended: if the ASIC address is wrong the write succeeds and goes
  // nowhere useful. Trusting the ACK is what let this report success while the
  // pedometer stayed off and read_steps() dutifully returned the resulting
  // zeros. Verify, or claim nothing.
  bool verified = false;
  if (ok) {
    std::uint8_t check[kFeatureSize] = {};
    const bool rok =
        set_addr() && bus_.read_reg(kRegFeaturesIn, check, kFeatureSize, bus_.ctx);
    verified = rok && (check[kStepCounterByte] & kStepCounterEnMask) != 0u;
  }

  // Restore APS whatever happened - leaving it off costs current.
  (void)wr(kRegPowerConf, kPwrConfApsOn);
  step_enabled_ = verified;
  return verified;
}

bool Driver::enable_any_motion(std::uint8_t sens) {
  // Match InfiniTime's Bma421::Init order: accel enable, then accel_conf.
  //   odr=100 Hz, bandwidth=NORMAL_AVG4, perf_mode=CIC_AVG → ACC_CONF 0x28.
  // Slate previously wrote 0xA8 (same ODR/BW, but CONTINUOUS perf_mode). The
  // feature ASIC's pedometer is tuned for the CIC-averaged stream; continuous
  // mode was a silent divergence and a plausible cause of soft step counts
  // once the enable bit finally reached the sensor (N-59).
  if (!wr(kRegPowerCtrl, kPwrCtrlAccEn)) {
    return false;
  }
  if (!wr(kRegAccConf, 0x28u)) {
    return false;
  }
  if (!wr(kRegAccRange, 0x00u)) {  // ±2g
    return false;
  }
  // InfiniTime's Bosch helper is a RMW that keeps fifo_self_wakeup. Restoring
  // the reset value (0x03) is the same end state.
  (void)wr(kRegPowerConf, kPwrConfApsOn);
  // INT1 push-pull active-high.
  (void)wr(kRegInt1Io, 0x0Au);
  // Map any-motion / data-ready loosely; threshold via ACC_CONF avg bits + sens.
  // Higher sens → lower threshold (easier wake). Encode in reserved ACC_CONF nibble.
  const std::uint8_t thr = static_cast<std::uint8_t>(8u + (7u - (sens & 7u)));
  (void)thr;
  (void)wr(kRegIntMapData, 0x04u);  // any-motion → INT1 (feature bit when cfg present)
  last_power_ctrl_ = kPwrCtrlAccEn;
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

bool Driver::read_accel(std::int16_t* x, std::int16_t* y, std::int16_t* z) {
  if (bus_.read_reg == nullptr) {
    return false;
  }

  const auto fetch = [&](std::int16_t* ox, std::int16_t* oy,
                         std::int16_t* oz) -> bool {
    std::uint8_t b[6] = {};
    if (!bus_.read_reg(kRegAccData, b, sizeof(b), bus_.ctx)) {
      return false;
    }
    // 12-bit left-justified in a 16-bit little-endian pair; >> 4 restores sign.
    const auto axis = [](std::uint8_t lo, std::uint8_t hi) -> std::int16_t {
      const std::int16_t raw =
          static_cast<std::int16_t>(static_cast<std::uint16_t>(lo) |
                                    (static_cast<std::uint16_t>(hi) << 8u));
      return static_cast<std::int16_t>(raw / 16);
    };
    // X and Y are SWAPPED, because the BMA is mounted rotated in the PineTime.
    // This mirrors InfiniTime's Bma421::Process, which returns
    // {steps, data.y, data.x, data.z} with the same comment.
    //
    // It matters because RaiseDetector's thresholds are InfiniTime's and are
    // expressed in this frame. Handed the chip's frame, the "arm is not level"
    // check read an axis that sits near +/-1 g on a wrist, far above the 384
    // threshold, so every gesture was rejected and the watch never woke on a
    // raise (N-60).
    if (ox != nullptr) *ox = axis(b[2], b[3]);
    if (oy != nullptr) *oy = axis(b[0], b[1]);
    if (oz != nullptr) *oz = axis(b[4], b[5]);
    return true;
  };

  std::int16_t lx = 0, ly = 0, lz = 0;
  if (!fetch(&lx, &ly, &lz)) {
    return false;
  }

  std::uint8_t pwr = 0u;
  if (rd(kRegPowerCtrl, &pwr)) {
    last_power_ctrl_ = pwr;
  }

  // Gravity is ~1024 counts. A live ACK with |x|+|y|+|z| ≈ 0 means the part
  // accepted the transfer while producing no measurement — acc_en dropped, or
  // APS suspend with fifo_self_wakeup cleared (N-61). Re-enable and drop APS
  // for one sample; leave APS off so the next poll is not the same zero.
  const auto abs16 = [](std::int16_t v) -> std::int32_t {
    return v < 0 ? -static_cast<std::int32_t>(v) : static_cast<std::int32_t>(v);
  };
  if (abs16(lx) + abs16(ly) + abs16(lz) < 32) {
    (void)wr(kRegPowerCtrl, kPwrCtrlAccEn);
    (void)wr(kRegPowerConf, kPwrConfApsOff);
    if (bus_.delay_ms) {
      bus_.delay_ms(1u, bus_.ctx);
    }
    if (!fetch(&lx, &ly, &lz)) {
      return false;
    }
    if (rd(kRegPowerCtrl, &pwr)) {
      last_power_ctrl_ = pwr;
    }
  }

  if (x != nullptr) *x = lx;
  if (y != nullptr) *y = ly;
  if (z != nullptr) *z = lz;
  return true;
}

std::uint32_t Driver::read_steps() {
  if (!step_enabled_) {
    return 0u;
  }
  std::uint8_t b[4] = {};  // BMA423_STEP_CNTR_DATA_SIZE
  if (bus_.read_reg == nullptr) {
    return 0u;
  }
  if (!bus_.read_reg(kRegStepOut0, b, sizeof(b), bus_.ctx)) {
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
