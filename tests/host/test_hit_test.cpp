#include "hit_test.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int g_failures = 0;

void expect_eq(const char* name, std::uint16_t got, std::uint16_t want) {
  if (got != want) {
    std::printf("FAIL %s: got %u want %u\n", name,
                static_cast<unsigned>(got), static_cast<unsigned>(want));
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

void test_miss_empty() {
  expect_eq("empty list", input::hit_test(10, 10, nullptr, 0), input::kNoHit);
}

void test_basic_hit() {
  const input::HitRect rects[] = {
      {1u, 10u, 10u, 40u, 40u},
  };
  expect_eq("inside", input::hit_test(20, 20, rects, 1), 1u);
  expect_eq("outside", input::hit_test(5, 5, rects, 1), input::kNoHit);
  expect_eq("edge exclusive max", input::hit_test(50, 10, rects, 1), input::kNoHit);
  expect_eq("edge inclusive min", input::hit_test(10, 10, rects, 1), 1u);
}

void test_zero_area() {
  const input::HitRect rects[] = {
      {1u, 0u, 0u, 0u, 40u},   // w=0
      {2u, 0u, 0u, 40u, 0u},   // h=0
      {3u, 0u, 0u, 0u, 0u},    // both
      {4u, 10u, 10u, 20u, 20u},
  };
  expect_eq("zero-w skipped", input::hit_test(0, 10, rects, 4), input::kNoHit);
  expect_eq("zero-h skipped", input::hit_test(10, 0, rects, 4), input::kNoHit);
  expect_eq("zero both skipped", input::hit_test(0, 0, rects, 4), input::kNoHit);
  expect_eq("valid after zeros", input::hit_test(15, 15, rects, 4), 4u);
}

void test_overlap_last_wins() {
  const input::HitRect rects[] = {
      {1u, 0u, 0u, 100u, 100u},
      {2u, 20u, 20u, 40u, 40u},
      {3u, 30u, 30u, 10u, 10u},
  };
  expect_eq("overlap centre = topmost", input::hit_test(35, 35, rects, 3), 3u);
  expect_eq("overlap mid layer", input::hit_test(25, 25, rects, 3), 2u);
  expect_eq("overlap base only", input::hit_test(5, 5, rects, 3), 1u);
}

void test_overlap_zero_does_not_block() {
  // A zero-area rect listed after a valid one must not steal the hit,
  // and must not prevent a later overlapping rect from winning.
  const input::HitRect rects[] = {
      {1u, 0u, 0u, 50u, 50u},
      {2u, 10u, 10u, 0u, 20u},   // zero-area in the middle
      {3u, 10u, 10u, 20u, 20u},
  };
  expect_eq("zero does not win", input::hit_test(15, 15, rects, 3), 3u);
  expect_eq("base still visible", input::hit_test(5, 5, rects, 3), 1u);
}

}  // namespace

int main() {
  test_miss_empty();
  test_basic_hit();
  test_zero_area();
  test_overlap_last_wins();
  test_overlap_zero_does_not_block();

  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return EXIT_FAILURE;
  }
  std::printf("all hit-test checks passed\n");
  return EXIT_SUCCESS;
}
