#include "rtt.hpp"

#include <cstddef>
#include <cstdint>

namespace {

struct RttBuffer {
  const char* name;
  char* buffer;
  std::uint32_t size;
  volatile std::uint32_t write_offset;
  volatile std::uint32_t read_offset;
  std::uint32_t flags;
};

struct RttControlBlock {
  char id[16];
  std::int32_t up_count;
  std::int32_t down_count;
  RttBuffer up[1];
  RttBuffer down[1];
};

char up_buffer[1024];
char down_buffer[16];

RttControlBlock g_rtt __attribute__((section(".data"))) = {
  "SEGGER RTT",
  1,
  1,
  {
    {"Terminal", up_buffer, sizeof(up_buffer), 0u, 0u, 0u},
  },
  {
    {"Terminal", down_buffer, sizeof(down_buffer), 0u, 0u, 0u},
  },
};

constexpr std::uint32_t kConfiguredLogLevel =
#ifdef SLATE_LOG_LEVEL
    SLATE_LOG_LEVEL;
#else
    3u;
#endif

char level_prefix(rtt::Level level) {
  switch (level) {
    case rtt::Level::Error:
      return 'E';
    case rtt::Level::Warn:
      return 'W';
    case rtt::Level::Info:
      return 'I';
    case rtt::Level::Debug:
      return 'D';
  }
  return '?';
}

void write_char(char c) {
#if defined(SLATE_ENABLE_RTT)
  auto& up = g_rtt.up[0];
  const std::uint32_t next = (up.write_offset + 1u) % up.size;
  if (next == up.read_offset) {
    return;
  }

  up.buffer[up.write_offset] = c;
  up.write_offset = next;
#else
  (void)c;
#endif
}

}  // namespace

namespace rtt {

void init() {
}

void write(const char* text) {
  while (*text != '\0') {
    write_char(*text++);
  }
}

void write_line(const char* text) {
  write(text);
  write("\r\n");
}

void write_hex(std::uint32_t value) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  write("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    write_char(kDigits[(value >> shift) & 0xFu]);
  }
}

void log(Level level, const char* text) {
  if (static_cast<std::uint32_t>(level) > kConfiguredLogLevel) {
    return;
  }

  write("[");
  write_char(level_prefix(level));
  write("] ");
  write_line(text);
}

}  // namespace rtt
