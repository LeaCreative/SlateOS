#pragma once

#include <cstdint>

namespace rtt {

enum class Level : std::uint32_t {
  Error = 1,
  Warn = 2,
  Info = 3,
  Debug = 4,
};

void init();
void write(const char* text);
void write_line(const char* text);
void write_hex(std::uint32_t value);
void log(Level level, const char* text);

}  // namespace rtt
