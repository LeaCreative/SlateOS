#include "alarm_sched.hpp"
#include "local_budgets.hpp"
#include "local_core.hpp"
#include "local_state.hpp"
#include "notif_store.hpp"
#include "persist.hpp"
#include "wall_clock.hpp"

#include <cstdio>
#include <cstring>

namespace {

int g_fails = 0;

void expect(const char* name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    ++g_fails;
  } else {
    std::printf("ok   %s\n", name);
  }
}

// Host persist: 3 RAM slots.
alignas(4) std::uint8_t g_slots[3][1024];
std::size_t g_slot_len[3] = {};

std::size_t host_read(slate::persist::Slot slot, void* dst, std::size_t cap,
                      void*) {
  const auto i = static_cast<std::uint8_t>(slot);
  if (i >= 3u || g_slot_len[i] == 0u) {
    return 0u;
  }
  const std::size_t n = g_slot_len[i] < cap ? g_slot_len[i] : cap;
  std::memcpy(dst, g_slots[i], n);
  return n;
}

bool host_write(slate::persist::Slot slot, const void* src, std::size_t len,
                void*) {
  const auto i = static_cast<std::uint8_t>(slot);
  if (i >= 3u || len > sizeof(g_slots[0])) {
    return false;
  }
  std::memcpy(g_slots[i], src, len);
  g_slot_len[i] = len;
  return true;
}

std::uint64_t g_ticks = 0u;
std::uint64_t host_ticks(void*) { return g_ticks; }

std::uint8_t g_last_list[512];
std::size_t g_last_list_len = 0u;
int g_haptic_count = 0;

void push_list(const std::uint8_t* data, std::size_t len, void*) {
  if (len > sizeof(g_last_list)) {
    len = sizeof(g_last_list);
  }
  std::memcpy(g_last_list, data, len);
  g_last_list_len = len;
}

void haptic(std::uint8_t, void*) { ++g_haptic_count; }

}  // namespace

static void test_budgets() {
  expect("local State <= 3KB",
         slate::local::state_bytes_used() <= slate::budget::kLocalScreenStateBytes);
  expect("local block == 3KB",
         slate::local::screen_block_bytes() == slate::budget::kLocalScreenStateBytes);
  expect("notif store <= 4KB",
         slate::notif::store_bytes_used() <= slate::budget::kNotifStoreBytes);
  std::printf("  local State=%zu block=%zu budget=%zu\n",
              slate::local::state_bytes_used(),
              slate::local::screen_block_bytes(),
              slate::budget::kLocalScreenStateBytes);
  std::printf("  notif Store=%zu budget=%zu\n",
              slate::notif::store_bytes_used(),
              slate::budget::kNotifStoreBytes);
}

static void test_clock_drift_and_persist() {
  std::memset(g_slots, 0, sizeof(g_slots));
  std::memset(g_slot_len, 0, sizeof(g_slot_len));
  slate::persist::Hooks ph{&host_read, &host_write, nullptr};
  slate::persist::init(ph);
  slate::clock::Hooks ch{&host_ticks, nullptr};
  slate::clock::init(ch);
  g_ticks = 0u;
  slate::clock::apply_cts_sync(1'700'000'000u);
  expect("synced", slate::clock::is_synced());
  expect("unix at sync", slate::clock::unix_time() == 1'700'000'000u);

  // Advance 1000 s of ticks with no drift.
  g_ticks = 1000ull * 32768ull;
  expect("unix +1000", slate::clock::unix_time() == 1'700'001'000u);

  // Phone reports +1100 s while local advanced 1000 s → positive ppm.
  slate::clock::apply_cts_sync(1'700'001'100u);
  const std::int32_t ppm = slate::clock::drift_ppm();
  expect("drift updated", ppm > 0);

  // Reload from persist.
  slate::clock::init(ch);
  slate::clock::load_persisted();
  expect("persist reload synced", slate::clock::is_synced());
  expect("persist drift kept", slate::clock::drift_ppm() == ppm);
}

static void test_notif_store() {
  slate::notif::Store store{};
  slate::notif::init(&store);
  // Build UPSERT matching SystemNotifCodec.
  const char* key = "pkg|1";
  const char* title = "Hi";
  const char* text = "Yo";
  std::uint8_t msg[64];
  std::size_t n = 0u;
  msg[n++] = slate::notif::kOpUpsert;
  msg[n++] = 5;
  std::memcpy(msg + n, key, 5);
  n += 5;
  msg[n++] = slate::notif::kFlagClearable;
  msg[n++] = 1;  // category
  msg[n++] = 'H';
  msg[n++] = 2;
  std::memcpy(msg + n, title, 2);
  n += 2;
  msg[n++] = 2;
  std::memcpy(msg + n, text, 2);
  n += 2;
  const std::uint32_t when = 42u;
  msg[n++] = static_cast<std::uint8_t>(when);
  msg[n++] = static_cast<std::uint8_t>(when >> 8);
  msg[n++] = static_cast<std::uint8_t>(when >> 16);
  msg[n++] = static_cast<std::uint8_t>(when >> 24);

  expect("upsert", slate::notif::on_system_message(&store, msg, n));
  expect("count 1", store.count == 1u);
  expect("title", store.entries[0].title_len == 2u);
  expect("when", store.entries[0].when_sec == 42u);

  slate::notif::mark_all_stale(&store, true);
  expect("stale", store.entries[0].stale == 1u);

  std::uint8_t rem[8] = {slate::notif::kOpRemove, 5};
  std::memcpy(rem + 2, key, 5);
  expect("remove", slate::notif::on_system_message(&store, rem, 7));
  expect("empty", store.count == 0u);
}

static void test_alarms() {
  std::memset(g_slots, 0, sizeof(g_slots));
  std::memset(g_slot_len, 0, sizeof(g_slot_len));
  slate::persist::Hooks ph{&host_read, &host_write, nullptr};
  slate::persist::init(ph);

  slate::alarm::Table table{};
  slate::alarm::init(&table);
  expect("add alarm", slate::alarm::add_alarm(&table, 7, 30, 0x7F));
  // wday=1 (Mon), 7:30
  auto ev = slate::alarm::poll(&table, 0u, 1u, 7u, 30u);
  expect("alarm fires", ev.kind == slate::alarm::FireKind::Alarm);
  ev = slate::alarm::poll(&table, 0u, 1u, 7u, 30u);
  expect("alarm once/day", ev.kind == slate::alarm::FireKind::None);

  expect("timer", slate::alarm::start_timer(&table, 10u, 1000u));
  ev = slate::alarm::poll(&table, 1009u, 0u, 0u, 0u);
  expect("timer not yet", ev.kind == slate::alarm::FireKind::None);
  ev = slate::alarm::poll(&table, 1010u, 0u, 0u, 0u);
  expect("timer fires", ev.kind == slate::alarm::FireKind::Timer);
}

static void test_core_alert() {
  std::memset(g_slots, 0, sizeof(g_slots));
  std::memset(g_slot_len, 0, sizeof(g_slot_len));
  g_ticks = 0u;
  g_haptic_count = 0;
  g_last_list_len = 0u;
  slate::persist::Hooks ph{&host_read, &host_write, nullptr};
  slate::persist::init(ph);
  slate::clock::Hooks ch{&host_ticks, nullptr};
  slate::clock::init(ch);
  slate::clock::apply_cts_sync(1'700'000'000u);

  slate::core::Core core;
  slate::core::Hooks hk;
  hk.push_list = &push_list;
  hk.haptic = &haptic;
  core.init(hk, nullptr);
  expect("face list", g_last_list_len > 0u);

  (void)slate::alarm::add_alarm(&core.alarms(), 0, 0, 0x7F);
  // Force civil midnight-ish: epoch already at sync; set alarm to current H:M.
  const auto c = slate::clock::civil_now();
  core.alarms().alarms[0].hour = c.hour;
  core.alarms().alarms[0].minute = c.minute;
  core.alarms().alarms[0].fired_today = 0u;
  core.tick(0u);
  expect("alert screen", core.local_state().screen == slate::local::Screen::Alert);
  expect("haptic on alert", g_haptic_count > 0);
}

int main() {
  test_budgets();
  test_clock_drift_and_persist();
  test_notif_store();
  test_alarms();
  test_core_alert();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all local-core tests passed\n");
  return 0;
}

