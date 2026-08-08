#pragma once

// Bosch BMA423 / BMA425 feature configuration blobs - see bma_config.cpp for
// the BSD-3-Clause notice and what they are for.

#include <cstddef>
#include <cstdint>

namespace slate {
namespace bma_cfg {

extern const std::uint8_t kBma423Config[6144];
extern const std::uint8_t kBma425Config[6144];

constexpr std::size_t kBma423ConfigLen = sizeof(kBma423Config);
constexpr std::size_t kBma425ConfigLen = sizeof(kBma425Config);

}  // namespace bma_cfg
}  // namespace slate
