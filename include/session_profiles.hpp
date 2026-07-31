#pragma once

#include "sdp_opcodes.hpp"

#include <cstddef>
#include <cstdint>

// Firmware-owned session profiles (§4.6). The phone SELECTS by id; it cannot
// invent names or override backlight / connection-interval parameters.

namespace slate {
namespace profile {

struct Desc {
  std::uint8_t id = 0u;
  const char* name = "";           // wire name, e.g. "ambient"
  std::uint8_t backlight = 40u;    // 0–100
  std::uint16_t interval_units = 24u;  // 1.25 ms units
};

constexpr std::uint8_t kIdAmbient   = 0u;
constexpr std::uint8_t kIdActive    = 1u;
constexpr std::uint8_t kIdStreaming = 2u;

constexpr Desc kCatalog[] = {
    {kIdAmbient,   "ambient",   0u,  400u},  // backlight off; 500 ms interval (§8)
    {kIdActive,    "active",    55u, 24u},   // 30 ms
    {kIdStreaming, "streaming", 70u, 12u},   // 15 ms
};

constexpr std::uint8_t kCatalogCount =
    static_cast<std::uint8_t>(sizeof(kCatalog) / sizeof(kCatalog[0]));

inline const Desc* find(std::uint8_t id) {
  for (std::uint8_t i = 0u; i < kCatalogCount; ++i) {
    if (kCatalog[i].id == id) {
      return &kCatalog[i];
    }
  }
  return nullptr;
}

inline const Desc* find_by_name(const char* name, std::size_t len) {
  if (name == nullptr) {
    return nullptr;
  }
  for (std::uint8_t i = 0u; i < kCatalogCount; ++i) {
    const char* n = kCatalog[i].name;
    std::size_t j = 0u;
    while (j < len && n[j] != '\0' && n[j] == name[j]) {
      ++j;
    }
    if (j == len && n[j] == '\0') {
      return &kCatalog[i];
    }
  }
  return nullptr;
}

}  // namespace profile
}  // namespace slate
