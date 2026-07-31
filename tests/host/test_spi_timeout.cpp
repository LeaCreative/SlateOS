#include "spi_bus.hpp"

#include <cstdio>

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_fails;
  }
}

}  // namespace

int main() {
  // 255 B @ 8 MHz ≈ 255 µs; budget is 4× + 200, clamped to 5 ms.
  expect(spi::end_wait_budget_us(1) < spi::kEndTimeoutUs, "small chunk < cap");
  expect(spi::end_wait_budget_us(255) <= spi::kEndTimeoutUs, "max chunk <= cap");
  expect(spi::end_wait_budget_us(255) >= 255u, "max chunk covers wire time");
  expect(spi::end_wait_budget_us(0) >= 1u, "zero bytes still positive");
  // Enormous hypothetical length still clamps.
  expect(spi::end_wait_budget_us(100000u) == spi::kEndTimeoutUs,
         "huge length clamps to kEndTimeoutUs");

  if (g_fails == 0) {
    std::printf("OK: spi_timeout\n");
    return 0;
  }
  std::printf("%d failure(s)\n", g_fails);
  return 1;
}
