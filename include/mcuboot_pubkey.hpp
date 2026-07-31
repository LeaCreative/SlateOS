#pragma once

#include <cstdint>

namespace slate {
namespace mcuboot {

const std::uint8_t* public_key();
std::uint32_t public_key_len();

}  // namespace mcuboot
}  // namespace slate
