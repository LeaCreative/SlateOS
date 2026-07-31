#include "ble_gap_adv_policy.hpp"

#include <cstdio>

namespace {

int g_failures = 0;

void expect_true(const char* name, bool cond) {
  if (!cond) {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

}  // namespace

int main() {
  using ble::gap_adv::EventKind;
  using ble::gap_adv::connect_event;
  using ble::gap_adv::should_resume;

  expect_true("connect ok: no resume",
              !should_resume(connect_event(0)));
  expect_true("connect fail: resume",
              should_resume(connect_event(1)));
  expect_true("connect fail neg: resume",
              should_resume(connect_event(-1)));
  expect_true("disconnect: resume",
              should_resume(EventKind::Disconnect));
  expect_true("other: no resume",
              !should_resume(EventKind::Other));

  if (g_failures != 0) {
    std::printf("%d failures\n", g_failures);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
