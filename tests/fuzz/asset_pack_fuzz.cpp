// libFuzzer harness for the asset pack index parser (M11).
#include "asset_pack.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  slate::pack::Index idx{};
  (void)slate::pack::parse(data, size, &idx);
  return 0;
}
