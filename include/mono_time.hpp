#pragma once

#include <cstdint>

// Monotonic millisecond clock for session / confirm / core cadences.
//
// Returns std::uint32_t that wraps at 2^32 (~49.7 days). Unsigned subtraction
// (now - then) is then modular-consistent across the wrap — unlike the old
// board::micros()/1000 path, which wrapped near 4.29e6 ms (~71.6 min) and made
// (500 - 4294000) look like ~4.29e9 ms elapsed.

namespace slate {
namespace time {

std::uint32_t mono_ms();

}  // namespace time
}  // namespace slate
