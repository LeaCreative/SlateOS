#pragma once

#include <cstddef>
#include <cstdint>

// Raise-to-wake ("wrist flick"), mirroring InfiniTime's MotionController.
//
// Deliberately free of drivers, FreeRTOS and Slate types: it takes accelerometer
// samples and answers one question. That is what lets the whole thing be tested
// on the desktop against synthetic gesture traces instead of on a wrist.
//
// Why this and not the BMA's hardware wrist-wear interrupt: InfiniTime does not
// use that either. The hardware feature lives in the sensor's feature ASIC and
// only works once the 6 KB Bosch config stream has loaded, whereas raw
// acceleration is live from power-on. Statistics over raw samples therefore work
// on any part in any state, and they are inspectable — a threshold that fires
// too eagerly can be seen and tuned, an opaque interrupt cannot.

namespace slate {
namespace motion {

/**
 * Thresholds, taken verbatim from InfiniTime's ShouldRaiseWake().
 *
 * Units are accelerometer counts at +/-2g, where 1g is about 1024. They are not
 * arbitrary: xThresh rejects an arm that is not level, varianceThresh rejects
 * samples carrying real acceleration rather than gravity, and rollDegrees is the
 * actual gesture.
 */
constexpr std::int32_t kVarianceThreshold = 56 * 56;
constexpr std::int16_t kXMeanThreshold = 384;
constexpr std::int16_t kYMeanThreshold = -64;
constexpr std::int16_t kYMeanFaceDownThreshold = -724;
constexpr std::int16_t kRollDegreesThreshold = -45;

/** Degrees, -90..90. `v` is scaled so that 1g maps to 32767. */
std::int16_t asin_degrees(std::int32_t v);

/**
 * Roll between two gravity vectors, in degrees.
 *
 * Only meaningful when the inputs really are gravity — hence the variance
 * rejection before this is ever consulted.
 */
std::int16_t degrees_rolled(std::int16_t y, std::int16_t z,
                            std::int16_t prev_y, std::int16_t prev_z);

class RaiseDetector {
public:
  /** Samples retained. At a 100 ms poll this is 0.8 s of history. */
  static constexpr std::size_t kHistory = 8u;
  /** Samples averaged at each end of that window. */
  static constexpr std::size_t kWindow = 2u;

  struct Stats {
    std::int16_t x_mean = 0, y_mean = 0, z_mean = 0;
    std::int16_t prev_x_mean = 0, prev_y_mean = 0, prev_z_mean = 0;
    std::int32_t x_var = 0, y_var = 0, z_var = 0;
  };

  void reset();

  /** Push one sample. Call at a steady cadence; the maths assumes even spacing. */
  void update(std::int16_t x, std::int16_t y, std::int16_t z);

  /** Mean of the newest two samples against the oldest two, plus variance. */
  Stats stats() const;

  /**
   * True when the last sample completed a raise gesture.
   *
   * False until the history has filled — a partly-filled buffer compares real
   * samples against zeros, which reads as a huge roll and would fire on the
   * first movement after every wake.
   */
  bool should_raise_wake() const;

private:
  std::int16_t at_x(std::size_t n) const { return x_[(idx_ + n) % kHistory]; }
  std::int16_t at_y(std::size_t n) const { return y_[(idx_ + n) % kHistory]; }
  std::int16_t at_z(std::size_t n) const { return z_[(idx_ + n) % kHistory]; }

  std::int16_t x_[kHistory] = {};
  std::int16_t y_[kHistory] = {};
  std::int16_t z_[kHistory] = {};
  std::size_t idx_ = 0u;
  std::size_t filled_ = 0u;
};

}  // namespace motion
}  // namespace slate
