// SDP display-list libFuzzer harness (M3 deliverable #1).
//
// Builds on the HOST with clang + libFuzzer — no nRF / Renderer linkage.
//
//   cmake -S tests/fuzz -B build/fuzz -DCMAKE_CXX_COMPILER=clang++
//   cmake --build build/fuzz
//   ./build/fuzz/sdp_parser_fuzz -max_len=4096 -timeout=2 corpus/
//
// The parser must never fault, abort, or read out of bounds on any input.

#include "sdp_parser.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Caps enforced inside parse: size > 4096 rejected without reading past size.
  sdp::ParseOptions opt;
  opt.max_ops = 512;
  opt.max_bytes = 4096;
  opt.execute = false;  // validate-only; no Renderer side effects

  sdp::ParseResult r = sdp::parse(data, size, opt, /*sink=*/nullptr);
  (void)r;  // Ok / Reject / Truncated — never abort/UB
  return 0;
}
