#include "shake_wake.hpp"

namespace slate {
namespace motion {

std::int32_t shake_threshold(Sensitivity s) {
  switch (s) {
    case Sensitivity::Soft:
      return 80;
    case Sensitivity::Hard:
      return 250;
    case Sensitivity::Normal:
    default:
      return 150;  // InfiniTime default
  }
}

void ShakeDetector::reset() {
  for (std::size_t i = 0u; i < kHistory; ++i) {
    x_[i] = 0;
    y_[i] = 0;
    z_[i] = 0;
  }
  idx_ = 0u;
  filled_ = 0u;
  speed_ = 0;
}

void ShakeDetector::update(std::int16_t x, std::int16_t y, std::int16_t z) {
  idx_ = (idx_ + 1u) % kHistory;
  x_[idx_] = x;
  y_[idx_] = y;
  z_[idx_] = z;
  if (filled_ < kHistory) {
    ++filled_;
  }
  if (filled_ < kHistory) {
    return;
  }
  // Oldest sample is one past newest in the ring (same walk as InfiniTime hist).
  const std::size_t oldest = (idx_ + 1u) % kHistory;
  const std::int32_t dx = static_cast<std::int32_t>(x_[idx_]) -
                          static_cast<std::int32_t>(x_[oldest]);
  const std::int32_t dy = static_cast<std::int32_t>(y_[idx_]) -
                          static_cast<std::int32_t>(y_[oldest]);
  const std::int32_t dz = static_cast<std::int32_t>(z_[idx_]) -
                          static_cast<std::int32_t>(z_[oldest]);
  std::int32_t weighted = dz + (dy / 2) + (dx / 4);
  if (weighted < 0) {
    weighted = -weighted;
  }
  const std::int32_t instant = weighted * 100 / kDtMs;
  // EMA: (.2 * speed) + (.8 * accumulated) as integer.
  speed_ = instant / 5 + speed_ * 4 / 5;
}

bool ShakeDetector::should_shake_wake() const {
  if (filled_ < kHistory) {
    return false;
  }
  return speed_ > shake_threshold(sens_);
}

}  // namespace motion
}  // namespace slate
