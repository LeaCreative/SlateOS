#pragma once

#include "raise_wake.hpp"

#include <cstdint>

// Shake-to-wake, mirroring InfiniTime's CurrentShakeSpeed / shakeWakeThreshold.
//
// Separate from raise: InfiniTime exposes shake as its own wake mode with a
// single scalar threshold (default 150). Soft/Normal/Hard map to 80/150/250.

namespace slate {
namespace motion {

/** Soft / Normal / Hard → InfiniTime-style threshold (lower = easier). */
std::int32_t shake_threshold(Sensitivity s);

/**
 * EMA of a weighted accel delta over an 8-sample window (same cadence as raise).
 *
 * InfiniTime formula (10 Hz poll):
 *   speed = abs(Δz + Δy/2 + Δx/4) * 100 / dt_ms
 *   accumulated = speed/5 + accumulated*4/5
 */
class ShakeDetector {
public:
  static constexpr std::size_t kHistory = 8u;
  /** Assumed poll interval in ms (matches Core::kRaisePollMs). */
  static constexpr std::int32_t kDtMs = 100;

  void reset();

  void set_sensitivity(Sensitivity s) { sens_ = s; }
  Sensitivity sensitivity() const { return sens_; }

  void update(std::int16_t x, std::int16_t y, std::int16_t z);

  std::int32_t speed() const { return speed_; }

  /** True when history is full and EMA exceeds the sensitivity threshold. */
  bool should_shake_wake() const;

private:
  std::int16_t x_[kHistory] = {};
  std::int16_t y_[kHistory] = {};
  std::int16_t z_[kHistory] = {};
  std::size_t idx_ = 0u;
  std::size_t filled_ = 0u;
  std::int32_t speed_ = 0;
  Sensitivity sens_ = Sensitivity::Normal;
};

}  // namespace motion
}  // namespace slate
