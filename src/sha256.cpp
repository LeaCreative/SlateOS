#include "sha256.hpp"

#include <cstring>

namespace slate {
namespace sha256 {
namespace {

std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
  return (x >> n) | (x << (32u - n));
}

void transform(Ctx& c, const std::uint8_t block[64]) {
  static const std::uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
      0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
      0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
      0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
      0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

  std::uint32_t w[64];
  for (std::uint32_t i = 0u; i < 16u; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4u]) << 24) |
           (static_cast<std::uint32_t>(block[i * 4u + 1u]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4u + 2u]) << 8) |
           static_cast<std::uint32_t>(block[i * 4u + 3u]);
  }
  for (std::uint32_t i = 16u; i < 64u; ++i) {
    const std::uint32_t s0 =
        rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
    const std::uint32_t s1 =
        rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  std::uint32_t a = c.state[0], b = c.state[1], cc = c.state[2], d = c.state[3];
  std::uint32_t e = c.state[4], f = c.state[5], g = c.state[6], h = c.state[7];

  for (std::uint32_t i = 0u; i < 64u; ++i) {
    const std::uint32_t S1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
    const std::uint32_t ch = (e & f) ^ ((~e) & g);
    const std::uint32_t t1 = h + S1 + ch + k[i] + w[i];
    const std::uint32_t S0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
    const std::uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    const std::uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = cc;
    cc = b;
    b = a;
    a = t1 + t2;
  }

  c.state[0] += a;
  c.state[1] += b;
  c.state[2] += cc;
  c.state[3] += d;
  c.state[4] += e;
  c.state[5] += f;
  c.state[6] += g;
  c.state[7] += h;
}

}  // namespace

void init(Ctx& c) {
  c.state[0] = 0x6a09e667u;
  c.state[1] = 0xbb67ae85u;
  c.state[2] = 0x3c6ef372u;
  c.state[3] = 0xa54ff53au;
  c.state[4] = 0x510e527fu;
  c.state[5] = 0x9b05688cu;
  c.state[6] = 0x1f83d9abu;
  c.state[7] = 0x5be0cd19u;
  c.bitlen = 0u;
  c.buflen = 0u;
}

void update(Ctx& c, const std::uint8_t* data, std::size_t len) {
  if (data == nullptr && len != 0u) {
    return;
  }
  for (std::size_t i = 0u; i < len; ++i) {
    c.buffer[c.buflen++] = data[i];
    if (c.buflen == 64u) {
      transform(c, c.buffer);
      c.bitlen += 512u;
      c.buflen = 0u;
    }
  }
}

void final(Ctx& c, std::uint8_t out[32]) {
  std::uint32_t i = c.buflen;
  c.buffer[i++] = 0x80u;
  if (i > 56u) {
    while (i < 64u) {
      c.buffer[i++] = 0u;
    }
    transform(c, c.buffer);
    i = 0u;
  }
  while (i < 56u) {
    c.buffer[i++] = 0u;
  }
  c.bitlen += static_cast<std::uint64_t>(c.buflen) * 8u;
  for (std::uint32_t j = 0u; j < 8u; ++j) {
    c.buffer[63u - j] = static_cast<std::uint8_t>((c.bitlen >> (8u * j)) & 0xFFu);
  }
  transform(c, c.buffer);
  for (std::uint32_t j = 0u; j < 8u; ++j) {
    out[j * 4u] = static_cast<std::uint8_t>((c.state[j] >> 24) & 0xFFu);
    out[j * 4u + 1u] = static_cast<std::uint8_t>((c.state[j] >> 16) & 0xFFu);
    out[j * 4u + 2u] = static_cast<std::uint8_t>((c.state[j] >> 8) & 0xFFu);
    out[j * 4u + 3u] = static_cast<std::uint8_t>(c.state[j] & 0xFFu);
  }
}

void hash(const std::uint8_t* data, std::size_t len, std::uint8_t out[32]) {
  Ctx c;
  init(c);
  update(c, data, len);
  final(c, out);
}

}  // namespace sha256
}  // namespace slate
