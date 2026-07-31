#pragma once

#include <cstddef>
#include <cstdint>

// Incremental SHA-256 (host + firmware). No dynamic allocation.

namespace slate {
namespace sha256 {

struct Ctx {
  std::uint32_t state[8]{};
  std::uint64_t bitlen = 0u;
  std::uint8_t buffer[64]{};
  std::uint32_t buflen = 0u;
};

void init(Ctx& c);
void update(Ctx& c, const std::uint8_t* data, std::size_t len);
void final(Ctx& c, std::uint8_t out[32]);

// One-shot helper.
void hash(const std::uint8_t* data, std::size_t len, std::uint8_t out[32]);

}  // namespace sha256
}  // namespace slate
