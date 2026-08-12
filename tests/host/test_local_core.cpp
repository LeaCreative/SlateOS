#include "alarm_sched.hpp"
#include "hit_test.hpp"
#include "local_budgets.hpp"
#include "local_core.hpp"
#include "local_state.hpp"
#include "notif_store.hpp"
#include "persist.hpp"
#include "sdp_opcodes.hpp"
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

int g_backlight = -1;
int g_panel_sleeps = 0;
int g_panel_wakes = 0;
bool g_panel_asleep = false;

void backlight_stub(std::uint8_t pct, void*) { g_backlight = pct; }

void display_sleep_stub(bool sleep, void*) {
  g_panel_asleep = sleep;
  if (sleep) {
    ++g_panel_sleeps;
  } else {
    ++g_panel_wakes;
  }
}

/**
 * Display sleep, the behaviour the operator actually sees.
 *
 * Every assertion here is about something that would be invisible until the
 * watch was on a wrist: a panel left lit, a wake that draws nothing, or a
 * screen that never comes back. None of it needs hardware to check.
 */
void test_display_sleep() {
  std::memset(g_slot_len, 0, sizeof(g_slot_len));
  g_backlight = -1;
  g_panel_sleeps = 0;
  g_panel_wakes = 0;
  g_panel_asleep = false;
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
  hk.backlight = &backlight_stub;
  hk.display_sleep = &display_sleep_stub;
  core.init(hk, nullptr);

  const std::uint32_t timeout = core.sleep_timeout_ms();
  expect("timeout is bounded", timeout >= 5000u && timeout <= 120000u);
  // Pinned: the owner asked for 20 s, and a default is the sort of thing that
  // drifts silently. If this changes it should be because someone meant it.
  expect("default display timeout is 20 s", timeout == 20000u);

  // Boot must not sleep the watch immediately. now_ms is small at boot, so a
  // zero-initialised activity stamp would have.
  core.tick(1000u);
  expect("awake shortly after boot", !core.sleeping());

  // Just short of the timeout, still awake.
  core.tick(1000u + timeout - 1u);
  expect("awake just before the timeout", !core.sleeping());

  core.tick(1000u + timeout);
  expect("asleep at the timeout", core.sleeping());
  expect("panel put to sleep", g_panel_sleeps == 1 && g_panel_asleep);
  expect("backlight off", g_backlight == 0);

  // Nothing local may paint into a sleeping panel.
  g_last_list_len = 0u;
  core.show_current();
  expect("no repaint while asleep", g_last_list_len == 0u);

  // Waking must bring the panel back BEFORE anything draws, and must ask for a
  // repaint — the panel's contents are undefined across SLPIN.
  core.wake_display();
  expect("awake after wake_display", !core.sleeping());
  expect("panel woken", g_panel_wakes == 1 && !g_panel_asleep);
  expect("backlight on", g_backlight > 0);
  expect("repaint requested on wake", core.take_paint_pending());

  // The digest must not survive sleep. If it did, a wake onto an unchanged
  // screen would draw nothing and the watch would sit lit and blank.
  g_last_list_len = 0u;
  core.show_current();
  expect("first paint after wake actually draws", g_last_list_len > 0u);

  // Activity defers sleep rather than merely delaying it once.
  //
  // tick() then note_activity(), which is the order the app loop runs them in:
  // input is polled at the top of an iteration and Core::tick() later, so a
  // stamp is taken from the most recent tick. note_activity() deliberately does
  // NOT wake, so this has to start from an awake watch — hence the wake_display
  // after the first tick.
  std::uint32_t t = 100000u;
  core.tick(t);
  core.wake_display();
  for (int i = 0; i < 10; ++i) {
    t += timeout - 100u;
    core.tick(t);
    core.note_activity();
    if (core.sleeping()) break;
  }
  expect("activity keeps the watch awake", !core.sleeping());

  // And it still sleeps once activity stops.
  t += timeout + 1u;
  core.tick(t);
  expect("sleeps again once activity stops", core.sleeping());
}

/** wake_seconds == 0 means never sleep — the bench case. */
void test_sleep_can_be_disabled() {
  std::memset(g_slot_len, 0, sizeof(g_slot_len));
  slate::persist::Hooks ph{&host_read, &host_write, nullptr};
  slate::persist::init(ph);
  slate::clock::Hooks ch{&host_ticks, nullptr};
  slate::clock::init(ch);

  slate::core::Core core;
  slate::core::Hooks hk;
  hk.push_list = &push_list;
  hk.backlight = &backlight_stub;
  hk.display_sleep = &display_sleep_stub;
  core.init(hk, nullptr);
  core.local_state().settings.wake_seconds = 0u;
  expect("timeout 0 disables sleep", core.sleep_timeout_ms() == 0u);

  core.tick(1000u);
  core.tick(1000u + 600000u);
  expect("never sleeps with the timeout disabled", !core.sleeping());
}

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

  expect("upsert",
         slate::notif::on_system_message(&store, msg, n) ==
             slate::notif::Ingest::StubNew);
  expect("count 1", store.count == 1u);
  expect("title", store.entries[0].title_len == 2u);
  expect("when", store.entries[0].when_sec == 42u);

  // Silent UPSERT of a new key retains the stub without StubNew (no haptic path).
  slate::notif::clear(&store);
  msg[7] = static_cast<std::uint8_t>(slate::notif::kFlagClearable |
                                     slate::notif::kFlagSilent);
  expect("silent new",
         slate::notif::on_system_message(&store, msg, n) ==
             slate::notif::Ingest::StubUpdate);
  expect("silent count", store.count == 1u);
  expect("silent flag stripped",
         (store.entries[0].flags & slate::notif::kFlagSilent) == 0u);
  expect("clearable kept",
         (store.entries[0].flags & slate::notif::kFlagClearable) != 0u);

  slate::notif::mark_all_stale(&store, true);
  expect("stale", store.entries[0].stale == 1u);

  std::uint8_t rem[8] = {slate::notif::kOpRemove, 5};
  std::memcpy(rem + 2, key, 5);
  expect("remove",
         slate::notif::on_system_message(&store, rem, 7) ==
             slate::notif::Ingest::Removed);
  expect("empty", store.count == 0u);

  // BODY parse
  std::uint8_t body[32];
  std::size_t bn = 0u;
  body[bn++] = slate::notif::kOpBody;
  body[bn++] = 5;
  std::memcpy(body + bn, key, 5);
  bn += 5;
  body[bn++] = 2;
  body[bn++] = 'O';
  body[bn++] = 'K';
  expect("body ingest",
         slate::notif::on_system_message(&store, body, bn) ==
             slate::notif::Ingest::Body);
  char kout[40];
  char tout[80];
  std::uint8_t kl = 0u;
  std::uint8_t tl = 0u;
  expect("body parse",
         slate::notif::parse_body(body, bn, kout, 40, &kl, tout, 80, &tl));
  expect("body text", tl == 2u && tout[0] == 'O' && tout[1] == 'K');
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

/**
 * A tap must reach the setting the row was drawn for.
 *
 * The settings screen's display list is built by one function and its taps are
 * handled by another, joined only by the element ids. Nothing else notices if
 * those drift, and the symptom on the wrist is simply "tapping that row does
 * nothing" — indistinguishable from a touch-driver problem.
 *
 * This walks the list the watch actually draws, takes the centre of each
 * touchable element, and asserts that tapping there changes the setting that
 * row shows. It is the same route the firmware takes: hit_test over the rects
 * the interpreter collected, then Core::on_tap_elem.
 */
static void test_settings_taps_reach_their_setting() {
  std::memset(g_slots, 0, sizeof(g_slots));
  std::memset(g_slot_len, 0, sizeof(g_slot_len));
  slate::persist::Hooks ph{&host_read, &host_write, nullptr};
  slate::persist::init(ph);
  slate::clock::Hooks ch{&host_ticks, nullptr};
  slate::clock::init(ch);

  slate::core::Core core;
  slate::core::Hooks hk;
  hk.push_list = &push_list;
  core.init(hk, nullptr);
  core.show_settings();
  expect("settings screen drawn", g_last_list_len > 0u);
  expect("settings is current",
         core.local_state().screen == slate::local::Screen::Settings);
  core.show_face();
  expect("show_face returns to the watch face",
         core.local_state().screen == slate::local::Screen::Face);
  // Re-open settings so the list below is the settings layout, not the face.
  core.show_settings();

  // Collect the touchable rects straight out of the list Core just pushed.
  input::HitRect rects[8];
  std::size_t n_rects = 0u;
  for (std::size_t i = 0u; i + 8u <= g_last_list_len && n_rects < 8u; ++i) {
    if (g_last_list[i] != sdp::op::BEGIN_ELEM) {
      continue;
    }
    input::HitRect r;
    r.id = static_cast<std::uint16_t>(g_last_list[i + 1u] |
                                      (g_last_list[i + 2u] << 8));
    r.x = g_last_list[i + 3u];
    r.y = g_last_list[i + 4u];
    r.w = g_last_list[i + 5u];
    r.h = g_last_list[i + 6u];
    r.flags = g_last_list[i + 7u];
    if ((r.flags & sdp::elem_flags::EMIT_TOUCH) != 0u) {
      rects[n_rects++] = r;
    }
  }
  expect("four touchable rows on page 0", n_rects == 4u);
  for (std::size_t i = 1u; i < n_rects; ++i) {
    expect("settings hit Y increases", rects[i].y > rects[i - 1u].y);
  }

  for (std::size_t i = 0u; i < n_rects; ++i) {
    const input::HitRect& r = rects[i];
    const std::uint16_t cx = static_cast<std::uint16_t>(r.x + r.w / 2u);
    const std::uint16_t cy = static_cast<std::uint16_t>(r.y + r.h / 2u);
    const std::uint16_t id =
        input::hit_test(cx, cy, rects, n_rects);
    expect("row centre hits its own element", id == r.id);

    const auto before = core.local_state().settings;
    expect("the tap is consumed by the watch", core.on_tap_elem(id));
    const auto after = core.local_state().settings;
    switch (id) {
      case slate::local::kSettingRaise:
        expect("raise row toggles raise-to-wake",
               after.tilt_enabled != before.tilt_enabled);
        break;
      case slate::local::kSettingRaiseSens:
        expect("raise sens cycles",
               after.raise_sensitivity != before.raise_sensitivity);
        break;
      case slate::local::kSettingShake:
        expect("shake row toggles shake-to-wake",
               after.shake_enabled != before.shake_enabled);
        break;
      case slate::local::kSettingShakeSens:
        expect("shake sens cycles",
               after.shake_sensitivity != before.shake_sensitivity);
        break;
      default:
        expect("no unexpected touchable element", false);
        break;
    }
  }

  expect("finger-up pages forward", core.on_settings_swipe(/*next=*/true));
  expect("on page 1",
         core.local_state().settings_sel == slate::local::kSettingsPageRows);
  n_rects = 0u;
  for (std::size_t i = 0u; i + 8u <= g_last_list_len && n_rects < 8u; ++i) {
    if (g_last_list[i] != sdp::op::BEGIN_ELEM) {
      continue;
    }
    input::HitRect r;
    r.id = static_cast<std::uint16_t>(g_last_list[i + 1u] |
                                      (g_last_list[i + 2u] << 8));
    r.x = g_last_list[i + 3u];
    r.y = g_last_list[i + 4u];
    r.w = g_last_list[i + 5u];
    r.h = g_last_list[i + 6u];
    r.flags = g_last_list[i + 7u];
    if ((r.flags & sdp::elem_flags::EMIT_TOUCH) != 0u) {
      rects[n_rects++] = r;
    }
  }
  expect("four touchable rows on page 1", n_rects == 4u);
  expect("timeout row toggles",
         core.on_tap_elem(slate::local::kSettingTimeout));
  expect("finger-down pages back", core.on_settings_swipe(/*next=*/false));
  expect("back on page 0", core.local_state().settings_sel == 0u);

  // A tap on empty space must NOT be swallowed: on a local screen an
  // unclaimed tap still belongs to the router, and before this the watch
  // forwarded every one of them.
  expect("a miss is not consumed", !core.on_tap_elem(input::kNoHit));

  // And the rows only answer while their own screen is up, so a stray element
  // id arriving from anywhere else cannot silently change a setting.
  core.local_state().screen = slate::local::Screen::Face;
  expect("no setting changes off the settings screen",
         !core.on_tap_elem(slate::local::kSettingSteps));
}

/**
 * A settings edit made on the watch must survive a reboot — including its
 * revision.
 *
 * Kept in RAM alone, the revision returned to 0 on every restart while the
 * phone kept counting. The phone then outranked the watch on the opening
 * exchange and put its own older copy back: the user's change on the watch
 * simply undid itself the next time the two connected. The watch reboots on
 * every flash, so this was not a rare case.
 */
static void test_settings_revision_survives_reboot() {
  std::memset(g_slots, 0, sizeof(g_slots));
  std::memset(g_slot_len, 0, sizeof(g_slot_len));
  g_ticks = 0u;
  slate::persist::Hooks ph{&host_read, &host_write, nullptr};
  slate::persist::init(ph);
  slate::clock::Hooks ch{&host_ticks, nullptr};
  slate::clock::init(ch);

  slate::core::Hooks hk;
  hk.push_list = &push_list;
  hk.haptic = &haptic;

  std::uint32_t rev_before = 0u;
  {
    slate::core::Core core;
    core.init(hk, nullptr);
    core.show_settings();
    // Two edits on the watch, so the revision is unambiguously past zero.
    core.on_tap_elem(slate::local::kSettingSteps);
    core.on_tap_elem(slate::local::kSettingTimeout);
    rev_before = core.settings_payload().revision;
    expect("watch edits advance the revision", rev_before >= 2u);
    expect("edits are marked for sending", core.take_settings_dirty());
  }

  // Reboot: a fresh Core reading the same persisted slots.
  slate::core::Core rebooted;
  rebooted.init(hk, nullptr);
  const auto after = rebooted.settings_payload();
  expect("revision survives the reboot", after.revision == rev_before);

  // The phone reconnects still holding the revision it had before the watch's
  // edits, with a different value. It must lose.
  slate::settings_sync::Payload stale;
  stale.revision = rev_before - 1u;
  stale.tilt_enabled = after.tilt_enabled ? 0u : 1u;
  stale.wake_seconds = 120u;
  stale.face_show_steps = after.face_show_steps ? 0u : 1u;
  expect("a stale phone copy is rejected", !rebooted.apply_settings_sync(stale));
  expect("watch settings unchanged",
         rebooted.settings_payload().face_show_steps == after.face_show_steps &&
             rebooted.settings_payload().wake_seconds == after.wake_seconds);

  // A genuinely newer phone copy still applies, and the next watch edit
  // outranks it rather than reusing a revision the phone has already spent.
  slate::settings_sync::Payload newer;
  newer.revision = rev_before + 5u;
  newer.tilt_enabled = 1u;
  newer.wake_seconds = 60u;
  newer.face_show_steps = 1u;
  expect("a newer phone copy applies", rebooted.apply_settings_sync(newer));
  rebooted.show_settings();
  rebooted.on_tap_elem(slate::local::kSettingSteps);
  expect("the next watch edit outranks the phone's",
         rebooted.settings_payload().revision > newer.revision);
}

int main() {
  test_budgets();
  test_clock_drift_and_persist();
  test_notif_store();
  test_alarms();
  test_core_alert();
  test_settings_taps_reach_their_setting();
  test_settings_revision_survives_reboot();
  test_display_sleep();
  test_sleep_can_be_disabled();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all local-core tests passed\n");
  return 0;
}

