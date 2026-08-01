#include "slate_uuids.hpp"

#include <cstdio>
#include <cstring>

// N-17: NimBLE stores 128-bit UUIDs fully byte-reversed relative to the text
// form. The arrays once reversed only within each group, so the watch
// advertised f378f04c-6ee9-62a9-44fa-0000e979acfb and no central could match
// the real service. These tests pin every array to the string the companion
// (SlateUuids.kt) uses, so the two can never drift apart again.

namespace {

int g_failures = 0;

void format_uuid(const std::uint8_t le[16], char out[37]) {
  // Reverse into text order, then punctuate 8-4-4-4-12.
  std::uint8_t be[16];
  for (int i = 0; i < 16; ++i) {
    be[i] = le[15 - i];
  }
  static const char* kHex = "0123456789abcdef";
  int o = 0;
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out[o++] = '-';
    }
    out[o++] = kHex[(be[i] >> 4) & 0xF];
    out[o++] = kHex[be[i] & 0xF];
  }
  out[o] = '\0';
}

void expect_uuid(const char* name, const std::uint8_t le[16], const char* want) {
  char got[37];
  format_uuid(le, got);
  if (std::strcmp(got, want) != 0) {
    std::printf("FAIL %s: got %s want %s\n", name, got, want);
    ++g_failures;
  } else {
    std::printf("PASS %s = %s\n", name, got);
  }
}

// The discriminant lives in the third text group; the rest must be identical
// across all four, or a central that finds the service cannot find the chars.
void expect_shared_base(const char* name, const std::uint8_t a[16]) {
  bool ok = true;
  for (int i = 0; i < 16; ++i) {
    if (i == 8 || i == 9) continue;  // discriminant bytes
    if (a[i] != slate::uuid::kService[i]) {
      ok = false;
    }
  }
  if (!ok) {
    std::printf("FAIL %s: base bytes differ from kService\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s shares the service base\n", name);
  }
}

}  // namespace

int main() {
  using namespace slate::uuid;
  expect_uuid("kService", kService, kServiceString);
  expect_uuid("kRx", kRx, kRxString);
  expect_uuid("kTx", kTx, kTxString);
  expect_uuid("kStatus", kStatus, kStatusString);

  expect_shared_base("kRx", kRx);
  expect_shared_base("kTx", kTx);
  expect_shared_base("kStatus", kStatus);

  // Guard the exact string constants too — the companion hard-codes these.
  if (std::strcmp(kServiceString, "e979acfb-c338-0000-a962-e96e4cf078f3") != 0) {
    std::printf("FAIL service string changed\n");
    ++g_failures;
  }

  // The wrong-endian value that shipped, so a regression is named on sight.
  char got[37];
  format_uuid(kService, got);
  if (std::strcmp(got, "f378f04c-6ee9-62a9-44fa-0000e979acfb") == 0) {
    std::printf("FAIL kService is the mixed-endian layout again (N-17)\n");
    ++g_failures;
  }

  if (g_failures != 0) {
    std::printf("%d FAILURES\n", g_failures);
    return 1;
  }
  std::printf("all uuid tests passed\n");
  return 0;
}
