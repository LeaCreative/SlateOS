#include "alarm_sched.hpp"

#include "persist.hpp"

#include <cstring>

namespace slate {
namespace alarm {

void init(Table* table) {
  if (table == nullptr) {
    return;
  }
  std::memset(table, 0, sizeof(*table));
}

void load(Table* table) {
  if (table == nullptr) {
    return;
  }
  Table tmp{};
  const std::size_t n = persist::load(persist::Slot::Alarms, &tmp, sizeof(tmp));
  if (n >= sizeof(tmp)) {
    *table = tmp;
  }
}

void save(const Table* table) {
  if (table == nullptr) {
    return;
  }
  (void)persist::save(persist::Slot::Alarms, table, sizeof(*table));
}

bool add_alarm(Table* table, std::uint8_t hour, std::uint8_t minute,
               std::uint8_t days_mask) {
  if (table == nullptr || table->alarm_count >= kMaxAlarms || hour > 23u ||
      minute > 59u) {
    return false;
  }
  Alarm& a = table->alarms[table->alarm_count];
  a.id = table->alarm_count;
  a.enabled = 1u;
  a.hour = hour;
  a.minute = minute;
  a.days_mask = days_mask == 0u ? 0x7Fu : days_mask;
  a.fired_today = 0u;
  ++table->alarm_count;
  save(table);
  return true;
}

bool start_timer(Table* table, std::uint16_t duration_s, std::uint32_t unix_now) {
  if (table == nullptr || duration_s == 0u || table->timer_count >= kMaxTimers) {
    return false;
  }
  // Reuse inactive slot if any.
  for (std::uint8_t i = 0u; i < kMaxTimers; ++i) {
    if (!table->timers[i].active) {
      table->timers[i].id = i;
      table->timers[i].active = 1u;
      table->timers[i].duration_s = duration_s;
      table->timers[i].fire_unix = unix_now + duration_s;
      if (i >= table->timer_count) {
        table->timer_count = static_cast<std::uint8_t>(i + 1u);
      }
      save(table);
      return true;
    }
  }
  return false;
}

bool cancel_timer(Table* table, std::uint8_t id) {
  if (table == nullptr || id >= kMaxTimers) {
    return false;
  }
  table->timers[id].active = 0u;
  table->timers[id].fire_unix = 0u;
  save(table);
  return true;
}

FireEvent poll(Table* table, std::uint32_t unix_now, std::uint8_t wday,
               std::uint8_t hour, std::uint8_t minute) {
  FireEvent ev{};
  if (table == nullptr) {
    return ev;
  }

  // Reset fired_today at midnight.
  if (hour == 0u && minute == 0u) {
    for (std::uint8_t i = 0u; i < table->alarm_count; ++i) {
      table->alarms[i].fired_today = 0u;
    }
  }

  for (std::uint8_t i = 0u; i < table->alarm_count; ++i) {
    Alarm& a = table->alarms[i];
    if (!a.enabled || a.fired_today) {
      continue;
    }
    if (((a.days_mask >> wday) & 1u) == 0u) {
      continue;
    }
    if (a.hour == hour && a.minute == minute) {
      a.fired_today = 1u;
      save(table);
      ev.kind = FireKind::Alarm;
      ev.id = a.id;
      return ev;
    }
  }

  for (std::uint8_t i = 0u; i < kMaxTimers; ++i) {
    Timer& t = table->timers[i];
    if (!t.active) {
      continue;
    }
    if (unix_now >= t.fire_unix) {
      t.active = 0u;
      save(table);
      ev.kind = FireKind::Timer;
      ev.id = t.id;
      return ev;
    }
  }
  return ev;
}

}  // namespace alarm
}  // namespace slate
