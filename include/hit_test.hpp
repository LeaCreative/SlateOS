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
  std::uint8_t  flags = 0u;  // sdp::elem_flags
};

// Map a tap (px, py) to a hit rect index (not id), or count if none.
// Overlaps: last matching rect wins. Skips NO_HIT at collect time; DISABLED
// still matches so callers can decide.
inline std::size_t hit_index(std::uint16_t px, std::uint16_t py,
                             const HitRect* rects, std::size_t count) {
  std::size_t hit = count;
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
      hit = i;
    }
  }
  return hit;
}

inline std::uint16_t hit_test(std::uint16_t px, std::uint16_t py,
                              const HitRect* rects, std::size_t count) {
  const std::size_t i = hit_index(px, py, rects, count);
  if (i >= count) {
    return kNoHit;
  }
  return rects[i].id;
}

}  // namespace input
