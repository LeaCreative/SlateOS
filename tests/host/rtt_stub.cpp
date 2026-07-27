#include "rtt.hpp"

// Host-test stub — no SEGGER RTT hardware.

namespace rtt {

void init() {}
void write(const char*) {}
void write_line(const char*) {}
void write_hex(std::uint32_t) {}
void log(Level, const char*) {}

}  // namespace rtt
