#pragma once

#include "local_budgets.hpp"

#include <cstddef>
#include <cstdint>

// Local alarms / countdown timers. Persistent via persist::Slot::Alarms.

namespace slate {
namespace alarm {

constexpr std::uint8_t kMaxAlarms = 8u;
constexpr std::uint8_t kMaxTimers = 4u;

struct Alarm {
  std::uint8_t id = 0u;
  std::uint8_t enabled = 0u;
  std::uint8_t hour = 0u;    // 0-23 local
  std::uint8_t minute = 0u;  // 0-59
  std::uint8_t days_mask = 0x7Fu;  // bit0=Sun … bit6=Sat; default daily
  std::uint8_t fired_today = 0u;
  std::uint16_t pad = 0u;
};

struct Timer {
  std::uint8_t id = 0u;
  std::uint8_t active = 0u;
  std::uint16_t duration_s = 0u;
  std::uint32_t fire_unix = 0u;  // absolute unix when it fires
};

struct Table {
  Alarm alarms[kMaxAlarms] = {};
  Timer timers[kMaxTimers] = {};
  std::uint8_t alarm_count = 0u;
  std::uint8_t timer_count = 0u;
  std::uint8_t pad[2] = {};
};

enum class FireKind : std::uint8_t { None = 0, Alarm, Timer };

struct FireEvent {
  FireKind kind = FireKind::None;
  std::uint8_t id = 0u;
};

void init(Table* table);
void load(Table* table);
void save(const Table* table);

// Returns first due event this tick (alarms then timers). Clears one-shot timer.
FireEvent poll(Table* table, std::uint32_t unix_now, std::uint8_t wday,
               std::uint8_t hour, std::uint8_t minute);

bool add_alarm(Table* table, std::uint8_t hour, std::uint8_t minute,
               std::uint8_t days_mask);
bool start_timer(Table* table, std::uint16_t duration_s, std::uint32_t unix_now);
bool cancel_timer(Table* table, std::uint8_t id);

}  // namespace alarm
}  // namespace slate
