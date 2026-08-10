#include "raise_wake.hpp"

namespace slate {
namespace motion {
namespace {

/**
 * sin(deg) * 32767 for 0..90.
 *
 * A second table rather than reusing the renderer's: that one is scaled to 256
 * for pixel work and, checked against it, its top entries do not reach 256, so
 * it is not accurate enough to invert. Precision matters here — asin is
 * steepest near 90 degrees, which is exactly where a wrist ends up.
 *
 * 182 bytes of flash, const, no RAM.
 */
constexpr std::int32_t kSinQ15[91] = {
        0,   572,  1144,  1715,  2286,  2856,  3425,  3993,  4560,  5126,
     5690,  6252,  6813,  7371,  7927,  8481,  9032,  9580, 10126, 10668,
    11207, 11743, 12275, 12803, 13328, 13848, 14365, 14876, 15384, 15886,
    16384, 16877, 17364, 17847, 18324, 18795, 19261, 19720, 20174, 20622,
    21063, 21498, 21926, 22348, 22763, 23170, 23571, 23965, 24351, 24730,
    25102, 25466, 25822, 26170, 26510, 26842, 27166, 27482, 27789, 28088,
    28378, 28660, 28932, 29197, 29452, 29698, 29935, 30163, 30382, 30592,
    30792, 30983, 31164, 31336, 31499, 31651, 31795, 31928, 32052, 32166,
    32270, 32365, 32449, 32524, 32588, 32643, 32688, 32723, 32748, 32763,
    32767,
};

std::int32_t clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

std::int16_t asin_degrees(std::int32_t v) {
  // Mirrors InfiniTime's Utility::Asin: a binary search over the sine table
  // rather than a table of asin, because asin is dense near the ends and a
  // uniformly sampled table of it would be worst exactly where it matters.
  const bool negative = v < 0;
  const std::int32_t a = clamp(negative ? -v : v, 0, 32767);

  std::int16_t low = 0;
  std::int16_t high = 90;
  std::int16_t angle = 45;
  while (low <= high) {
    const std::int32_t s = kSinQ15[angle];
    const std::int32_t s_sub = kSinQ15[angle > 0 ? angle - 1 : 0];
    const std::int32_t s_add = kSinQ15[angle < 90 ? angle + 1 : 90];
    if (a >= s_sub && a <= s_add) {
      if (a <= (s_sub + s) / 2) {
        --angle;
      } else if (a > (s + s_add) / 2) {
        ++angle;
      }
      break;
    }
    if (a < s) {
      high = static_cast<std::int16_t>(angle - 1);
    } else {
      low = static_cast<std::int16_t>(angle + 1);
    }
    angle = static_cast<std::int16_t>((low + high) / 2);
    if (angle < 0) {
      angle = 0;
    } else if (angle > 90) {
      angle = 90;
    }
  }
  return negative ? static_cast<std::int16_t>(-angle) : angle;
}

std::int16_t degrees_rolled(std::int16_t y, std::int16_t z,
                            std::int16_t prev_y, std::int16_t prev_z) {
  // *32 normalises an accelerometer count (1g ~ 1024) onto the table's full
  // scale. The z sign tests pick the correct quadrant: asin alone cannot tell
  // "rolled 30 degrees" from "rolled 150 degrees", and a watch face pointing at
  // the floor is exactly the case that matters.
  const std::int16_t prev_angle =
      asin_degrees(clamp(static_cast<std::int32_t>(prev_y) * 32, -32767, 32767));
  const std::int16_t angle =
      asin_degrees(clamp(static_cast<std::int32_t>(y) * 32, -32767, 32767));

  if (z < 0 && prev_z < 0) {
    return static_cast<std::int16_t>(angle - prev_angle);
  }
  if (prev_z < 0) {
    if (y < 0) {
      return static_cast<std::int16_t>(-prev_angle - angle - 180);
    }
    return static_cast<std::int16_t>(-prev_angle - angle + 180);
  }
  if (z < 0) {
    if (y < 0) {
      return static_cast<std::int16_t>(prev_angle + angle + 180);
    }
    return static_cast<std::int16_t>(prev_angle + angle - 180);
  }
  return static_cast<std::int16_t>(prev_angle - angle);
}

void RaiseDetector::reset() {
  for (std::size_t i = 0u; i < kHistory; ++i) {
    x_[i] = 0;
    y_[i] = 0;
    z_[i] = 0;
  }
  idx_ = 0u;
  filled_ = 0u;
}

void RaiseDetector::update(std::int16_t x, std::int16_t y, std::int16_t z) {
  idx_ = (idx_ + 1u) % kHistory;
  x_[idx_] = x;
  y_[idx_] = y;
  z_[idx_] = z;
  if (filled_ < kHistory) {
    ++filled_;
  }
}

RaiseDetector::Stats RaiseDetector::stats() const {
  Stats s;
  std::int32_t xm = 0, ym = 0, zm = 0;
  std::int32_t pxm = 0, pym = 0, pzm = 0;
  for (std::size_t i = 0u; i < kWindow; ++i) {
    // [kHistory - i] walks back from the newest; [1 + i] walks forward from the
    // oldest. Same indexing as InfiniTime's CircularBuffer, written out.
    xm += at_x(kHistory - i);
    ym += at_y(kHistory - i);
    zm += at_z(kHistory - i);
    pxm += at_x(1u + i);
    pym += at_y(1u + i);
    pzm += at_z(1u + i);
  }
  s.x_mean = static_cast<std::int16_t>(xm / static_cast<std::int32_t>(kWindow));
  s.y_mean = static_cast<std::int16_t>(ym / static_cast<std::int32_t>(kWindow));
  s.z_mean = static_cast<std::int16_t>(zm / static_cast<std::int32_t>(kWindow));
  s.prev_x_mean = static_cast<std::int16_t>(pxm / static_cast<std::int32_t>(kWindow));
  s.prev_y_mean = static_cast<std::int16_t>(pym / static_cast<std::int32_t>(kWindow));
  s.prev_z_mean = static_cast<std::int16_t>(pzm / static_cast<std::int32_t>(kWindow));

  for (std::size_t i = 0u; i < kWindow; ++i) {
    const std::int32_t dx = at_x(kHistory - i) - s.x_mean;
    const std::int32_t dy = at_y(kHistory - i) - s.y_mean;
    const std::int32_t dz = at_z(kHistory - i) - s.z_mean;
    s.x_var += dx * dx;
    s.y_var += dy * dy;
    s.z_var += dz * dz;
  }
  s.x_var /= static_cast<std::int32_t>(kWindow);
  s.y_var /= static_cast<std::int32_t>(kWindow);
  s.z_var /= static_cast<std::int32_t>(kWindow);
  return s;
}

std::uint8_t RaiseDetector::reject_code() const {
  // A partly-filled history compares real samples against the zeros it was
  // constructed with, which reads as a huge roll — the watch would wake on the
  // first movement after every sleep.
  if (filled_ < kHistory) {
    return 1u;
  }
  const Stats s = stats();

  // Arm not level: this is a reach or a swing, not a look at the watch.
  const std::int16_t ax = s.x_mean < 0 ? static_cast<std::int16_t>(-s.x_mean) : s.x_mean;
  if (ax > kXMeanThreshold) {
    return 2u;
  }
  // Variance above the threshold means the samples carry real acceleration, so
  // they are not a gravity vector and the roll below would be meaningless.
  if (s.y_var > kVarianceThreshold) {
    return 3u;
  }
  if (s.y_mean < kYMeanFaceDownThreshold && s.z_var > kVarianceThreshold) {
    return 4u;
  }
  if (s.y_mean > kYMeanThreshold) {
    return 5u;
  }
  if (!(degrees_rolled(s.y_mean, s.z_mean, s.prev_y_mean, s.prev_z_mean) <
        kRollDegreesThreshold)) {
    return 6u;
  }
  return 0u;
}

}  // namespace motion
}  // namespace slate
