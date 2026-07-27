#pragma once

#include <cstddef>
#include <cstdint>

// Host-safe hit-test helper — no firmware dependencies.
// Used by the watch input layer and by desktop unit tests.

namespace input {

constexpr std::uint16_t kNoHit = 0xFFFFu;

struct HitRect {
  std::uint16_t id = 0u;
  std::uint16_t x  = 0u;
  std::uint16_t y  = 0u;
  std::uint16_t w  = 0u;
  std::uint16_t h  = 0u;
};

// Map a tap (px, py) to an element id.
//
// Rules:
// - Half-open rectangles: [x, x+w) × [y, y+h).
// - Zero-area rects (w==0 or h==0) never hit.
// - Overlaps: the *last* matching rect in the list wins (painter / z-order).
inline std::uint16_t hit_test(std::uint16_t px, std::uint16_t py,
                              const HitRect* rects, std::size_t count) {
  std::uint16_t hit = kNoHit;
  if (rects == nullptr) {
    return hit;
  }

  for (std::size_t i = 0u; i < count; ++i) {
    const HitRect& r = rects[i];
    if (r.w == 0u || r.h == 0u) {
      continue;
    }
    if (px >= r.x && px < static_cast<std::uint16_t>(r.x + r.w) &&
        py >= r.y && py < static_cast<std::uint16_t>(r.y + r.h)) {
      hit = r.id;
    }
  }
  return hit;
}

}  // namespace input
