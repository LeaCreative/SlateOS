#include "local_core.hpp"

#include "battery.hpp"
#include "bma_config.hpp"
#include "local_budgets.hpp"
#include "local_ui.hpp"
#include "persist.hpp"
#include "sdp_opcodes.hpp"
#include "wall_clock.hpp"

#include <cstring>

namespace slate {
namespace core {

void Core::init(const Hooks& hooks, bma::Driver* bma) {
  hooks_ = hooks;
  bma_ = bma;
  std::memset(&block_, 0, sizeof(block_));
  block_.state.screen = local::Screen::Face;
  block_.state.battery_pct = battery::kPercentUnknown;
  // 0xFF = no config upload attempted. The memset above would otherwise leave
  // this at 0, which is indistinguishable from a real INTERNAL_STATUS of 0.
  block_.state.diag_bma_status = 0xFFu;
  notif::init(&notifs_);
  alarm::init(&alarms_);
  alarm::load(&alarms_);
  load_settings();
  clock::load_persisted();
  if (bma_ != nullptr && bma_->ok()) {
    // The right blob for the detected part. Passing nullptr here is what left
    // the pedometer dead: the BMA's feature ASIC has no firmware at power-on,
    // so without this stream every feature register reads zero.
    const std::uint8_t* cfg = nullptr;
    std::size_t cfg_len = 0u;
    if (bma_->chip() == bma::Chip::BMA425) {
      cfg = bma_cfg::kBma425Config;
      cfg_len = bma_cfg::kBma425ConfigLen;
    } else if (bma_->chip() == bma::Chip::BMA421) {
      cfg = bma_cfg::kBma423Config;  // the 421 runs the 423 stream
      cfg_len = bma_cfg::kBma423ConfigLen;
    }
    // No new diag field for "did the blob load": the step count is already the
    // honest indicator. Non-zero after a walk means the ASIC came up; a
    // permanent 0 means it did not.
    (void)bma_->configure(cfg, cfg_len, block_.state.settings.tilt_sensitivity);
    local_state().diag_bma_chip = static_cast<std::uint8_t>(bma_->chip());
    local_state().diag_bma_status = bma_->last_init_status();
    local_state().diag_bma_step_en = bma_->step_counter_enabled() ? 1u : 0u;
  }
  show_current();
}

BudgetReport Core::budgets() const {
  BudgetReport r;
  r.local_state_bytes = local::state_bytes_used();
  r.local_block_bytes = local::screen_block_bytes();
  r.local_budget = budget::kLocalScreenStateBytes;
  r.notif_store_bytes = notif::store_bytes_used();
  r.notif_budget = budget::kNotifStoreBytes;
  return r;
}

void Core::load_settings() {
  local::Settings s{};
  const std::size_t n = persist::load(persist::Slot::Settings, &s, sizeof(s));
  if (n >= sizeof(s) && s.magic == local::kSettingsMagic) {
    local_state().settings = s;
  } else {
    // Defaults come from the member initialisers in local::Settings and
    // nowhere else. They used to be restated here as well, so changing the
    // display timeout in the struct did nothing at all — this branch quietly
    // put the old value back on every watch that had no settings stored.
    local_state().settings = local::Settings{};
    local_state().settings.magic = local::kSettingsMagic;
  }
  // The persisted revision is the correct floor for the next local edit: every
  // higher one either side has issued has already been folded into it.
  highest_seen_rev_ = local_state().settings.revision;
}

void Core::save_settings() {
  local_state().settings.magic = local::kSettingsMagic;
  (void)persist::save(persist::Slot::Settings, &local_state().settings,
                      sizeof(local_state().settings));
}

// Link transitions only mark the face stale (N-15). A repaint costs hundreds
// of ms, and painting synchronously on connect/disconnect put that cost right
// where the central is discovering services. InfiniTime's BLE layer likewise
// posts a message and lets the display task redraw on its own schedule; the
// next scheduled paint picks the new state up.
void Core::show_not_connected() {
  wake_display();
  local_state().screen = local::Screen::Disconnected;
  show_current();
}

void Core::on_link_up() {
  local_state().link_up = 1u;
  local_state().remote_stale = 0u;
  notif::mark_all_stale(&notifs_, false);
  if (local_state().screen == local::Screen::Disconnected) {
    local_state().screen = local::Screen::Face;
  }
  paint_pending_ = true;
}

void Core::on_link_down() {
  local_state().link_up = 0u;
  notif::mark_all_stale(&notifs_, true);
  paint_pending_ = true;
}

bool Core::take_paint_pending() {
  const bool p = paint_pending_;
  paint_pending_ = false;
  return p;
}

void Core::set_remote_stale(bool stale) {
  local_state().remote_stale = stale ? 1u : 0u;
  if (local_state().screen == local::Screen::Face) {
    show_current();
  }
}

void Core::on_system_message(const std::uint8_t* msg, std::size_t len) {
  if (notif::on_system_message(&notifs_, msg, len)) {
    if (local_state().screen == local::Screen::Notifs) {
      show_current();
    }
  }
}

void Core::apply_cts_time(std::uint32_t unix_epoch_sec) {
  clock::apply_cts_sync(unix_epoch_sec);
  show_current();
}

void Core::show_ota_progress(std::uint8_t pct) {
  if (hooks_.push_list == nullptr) {
    return;
  }
  // Anything but show_current() writes the panel with a list Core did not
  // record, so the digest no longer describes what is displayed. Forgetting it
  // here is what stops a later "nothing changed" from skipping the paint that
  // puts the real screen back.
  forget_pushed_digest();
  const std::size_t n = ui::build_ota_banner(dl_buf_, sizeof(dl_buf_), pct);
  if (n > 0u) {
    hooks_.push_list(dl_buf_, n, hooks_.ctx);
  }
}

void Core::show_diag_band() {
#if SLATE_DIAG_OVERLAY
  if (hooks_.push_list == nullptr) {
    return;
  }
  // Anything but show_current() writes the panel with a list Core did not
  // record, so the digest no longer describes what is displayed. Forgetting it
  // here is what stops a later "nothing changed" from skipping the paint that
  // puts the real screen back.
  forget_pushed_digest();
  const std::size_t n =
      ui::build_diag_banner(dl_buf_, sizeof(dl_buf_), local_state());
  if (n > 0u) {
    hooks_.push_list(dl_buf_, n, hooks_.ctx);
  }
#endif
}

void Core::show_clock_band() {
  if (hooks_.push_list == nullptr) {
    return;
  }
  // Anything but show_current() writes the panel with a list Core did not
  // record, so the digest no longer describes what is displayed. Forgetting it
  // here is what stops a later "nothing changed" from skipping the paint that
  // puts the real screen back.
  forget_pushed_digest();
  const std::size_t n = ui::build_clock_band(dl_buf_, sizeof(dl_buf_));
  if (n > 0u) {
    hooks_.push_list(dl_buf_, n, hooks_.ctx);
  }
}

void Core::show_current() {
  if (hooks_.push_list == nullptr) {
    return;
  }
  // One guard, here, rather than at each call site. Gating callers one at a
  // time missed a different path three times running (N-29, N-30): the diag
  // overlay, the minute rollover, the stale hook — and still others call this
  // indirectly, `apply_cts_time` among them, which repaints on every time
  // sync. Every local repaint passes through here, so this is the only place
  // the rule can be enforced completely.
  if (!local_owns_screen_) {
    return;
  }
  // Same rule for a firmware transfer: the OTA banner owns the panel, and a
  // local repaint slipping in made the face and the banner alternate. Gating
  // the callers individually is what failed three times before — the guard
  // belongs here, where every local repaint passes.
  if (updating_) {
    return;
  }
  // Asleep: the panel is in SLPIN and its contents are undefined. Drawing into
  // it would cost a full render for pixels nobody can see, and would leave the
  // digest describing a screen the panel never showed.
  if (power_ == Power::Sleeping) {
    return;
  }
  ui::ViewModel vm;
  vm.state = &local_state();
  vm.notifs = &notifs_;
  vm.alarms = &alarms_;
  const std::size_t n = ui::build_screen(vm, dl_buf_, sizeof(dl_buf_));
  if (n == 0u) {
    return;
  }
  // Nothing changed since the last push, so there is nothing to draw.
  //
  // Building the list is microseconds; pushing it is three parses and a
  // 30-tile rasterise, ~236 ms, during which the panel visibly clears and
  // refills — the flashing the operator reported. Several callers repaint
  // unconditionally because they cannot cheaply tell whether their change is
  // visible; the loudest is set_remote_stale(), which the app loop calls every
  // 200 ms with a flag that almost never changes. Comparing the bytes that
  // would be drawn answers that question for all of them at once, and does it
  // where every local repaint already passes.
  const std::uint32_t d = digest_of(dl_buf_, n);
  if (d == pushed_digest_) {
    return;
  }
  pushed_digest_ = d;
  hooks_.push_list(dl_buf_, n, hooks_.ctx);
}

std::uint32_t Core::sleep_timeout_ms() const {
  // Reuses the existing wake_seconds setting rather than inventing a second
  // one. 0 disables sleep entirely, which is what a bench session wants.
  const std::uint32_t s = local_state().settings.wake_seconds;
  if (s == 0u) {
    return 0u;
  }
  const std::uint32_t clamped = (s < kMinSleepSeconds)   ? kMinSleepSeconds
                                : (s > kMaxSleepSeconds) ? kMaxSleepSeconds
                                                         : s;
  return clamped * 1000u;
}

void Core::enter_sleep() {
  if (power_ == Power::Sleeping) {
    return;
  }
  power_ = Power::Sleeping;
  display_on_ = false;
  // Backlight first, panel second. The other order shows one frame of a
  // sleeping panel lit up, which reads as a glitch.
  if (hooks_.backlight) {
    hooks_.backlight(0u, hooks_.ctx);
  }
  if (hooks_.display_sleep) {
    hooks_.display_sleep(true, hooks_.ctx);
  }
  // Panel contents are undefined across SLPIN, so nothing Core believes about
  // the display survives. Without this the first wake could match the digest
  // and skip the repaint, leaving the watch awake with a blank screen.
  forget_pushed_digest();
}

void Core::wake_display() {
  const bool was_sleeping = (power_ == Power::Sleeping);
  power_ = Power::Running;
  display_on_ = true;
  last_activity_ms_ = last_tick_ms_;
  activity_seeded_ = true;
  if (was_sleeping) {
    // Panel out of sleep BEFORE anything can draw: a push into a sleeping
    // ST7789 is accepted and discarded, so the screen would stay black until
    // whatever came next happened to repaint.
    if (hooks_.display_sleep) {
      hooks_.display_sleep(false, hooks_.ctx);
    }
    forget_pushed_digest();
    // Coalesced rather than painted inline — app_loop drains this once per
    // iteration, and a wake often arrives with other reasons to repaint.
    paint_pending_ = true;
  }
  if (hooks_.backlight) {
    hooks_.backlight(55u, hooks_.ctx);
  }
}

void Core::enter_alert(std::uint8_t kind, std::uint8_t id) {
  local_state().alert_kind = kind;
  local_state().alert_id = id;
  local_state().screen = local::Screen::Alert;
  if (hooks_.haptic) {
    hooks_.haptic(sdp::haptic_pattern::DOUBLE, hooks_.ctx);
  }
  wake_display();
  show_current();
}

void Core::poll_steps() {
  if (bma_ == nullptr || !bma_->ok()) {
    return;
  }
  const std::uint32_t raw = bma_->read_steps();
  const std::uint32_t now = clock::unix_time();
  const clock::Civil c = clock::civil_now();
  // Local midnight epoch approximation.
  const std::uint32_t midnight =
      now - (static_cast<std::uint32_t>(c.hour) * 3600u +
             static_cast<std::uint32_t>(c.minute) * 60u + c.second);
  if (local_state().day_epoch == 0u || midnight != local_state().day_epoch) {
    local_state().day_epoch = midnight;
    local_state().steps_at_day_start = raw;
  }
  local_state().steps =
      (raw >= local_state().steps_at_day_start) ? (raw - local_state().steps_at_day_start) : raw;
}

void Core::poll_battery(std::uint32_t now_ms) {
  const bool chg = battery::charging();
  const bool was = local_state().charging != 0u;
  const bool edge = chg != was;
  // InfiniTime: ReadPowerState + voltage on charging GPIOTE and on a timer.
  // ~1 s pin poll; SAADC (≤10 ms) on edge or every 10 s steady-state.
  if (hooks_.sample_battery != nullptr &&
      (edge || last_adc_ms_ == 0u ||
       static_cast<std::uint32_t>(now_ms - last_adc_ms_) >= 10000u)) {
    hooks_.sample_battery(hooks_.ctx);
    last_adc_ms_ = now_ms == 0u ? 1u : now_ms;
  }
  local_state().battery_pct = battery::percent();
  local_state().charging = chg ? 1u : 0u;
  if (local_state().screen == local::Screen::Charging) {
    local_state().screen = local::Screen::Face;
    show_current();
    return;
  }
  // Both edges wake, matching InfiniTime's OnChargingEvent -> GoToRunning.
  // Only the plug-in edge used to, so taking the watch off the charger left it
  // asleep — which is exactly the moment you look at it.
  if (chg != was) {
    wake_display();
    if (local_state().screen == local::Screen::Face) {
      show_current();
    }
  }
}

void Core::poll_alarms() {
  if (!clock::is_synced()) {
    return;
  }
  const clock::Civil c = clock::civil_now();
  const alarm::FireEvent ev =
      alarm::poll(&alarms_, clock::unix_time(), c.wday, c.hour, c.minute);
  if (ev.kind == alarm::FireKind::Alarm) {
    enter_alert(1u, ev.id);
  } else if (ev.kind == alarm::FireKind::Timer) {
    enter_alert(2u, ev.id);
  }
}

/**
 * Raise-to-wake, from polled acceleration.
 *
 * Runs only while asleep. Awake, the screen is already lit and the samples buy
 * nothing but an I2C transfer every 100 ms — and the detector is deliberately
 * fed nothing in that state, so waking always starts from a clean history
 * rather than one full of whatever the wrist did while the user was reading.
 *
 * The any-motion interrupt that used to live here is gone. It fired on any
 * movement above a threshold, which is not a gesture; InfiniTime does not use
 * the sensor's interrupt for this either, and for the same reason.
 */
void Core::poll_raise(std::uint32_t now_ms) {
  if (bma_ == nullptr || !bma_->ok()) {
    return;
  }
  if (!local_state().settings.tilt_enabled) {
    raise_.reset();
    return;
  }
  if (power_ != Power::Sleeping) {
    // Awake: keep the history empty so the next sleep starts cold.
    raise_.reset();
    return;
  }
  if (static_cast<std::uint32_t>(now_ms - last_raise_ms_) < kRaisePollMs) {
    return;
  }
  last_raise_ms_ = now_ms;

  std::int16_t x = 0, y = 0, z = 0;
  if (!bma_->read_accel(&x, &y, &z)) {
    return;
  }
  raise_.update(x, y, z);
  if (raise_.should_raise_wake()) {
    // Clear before waking: the samples that fired are spent, and leaving them
    // would let the next sleep re-fire on stale history.
    raise_.reset();
    wake_display();
    if (local_state().screen == local::Screen::Face ||
        local_state().screen == local::Screen::Disconnected) {
      // Coalesced with any other repaint this tick — see Core::tick.
      mark_paint_pending();
    }
  }
}

void Core::tick(std::uint32_t now_ms) {
  last_tick_ms_ = now_ms;
  if (!activity_seeded_) {
    // Seeded on the first tick, not at construction: now_ms is small at boot
    // and a zero start would sleep the watch seconds after it powers on.
    last_activity_ms_ = now_ms;
    activity_seeded_ = true;
  }
  // Cadences use unsigned (now - last); requires mono_ms wrap at 2^32.
  if (now_ms - last_step_ms_ >= 2000u) {
    const std::uint32_t steps_before = local_state().steps;
    poll_steps();
    last_step_ms_ = now_ms;
    // Repaint only when something on the face actually changed (N-13). A full
    // face render costs hundreds of ms — the display list is re-parsed once
    // per tile — so repainting every 2 s regardless burned most of the app
    // loop and starved the BLE host. Steps or the displayed minute changing
    // are the only reasons the face differs.
    const std::uint8_t minute_now = clock::civil_now().minute;
    // local_owns_screen_: the minute rolling over must not wipe a screen the
    // phone owns (N-29). Step and battery bookkeeping still run.
    if (local_state().screen == local::Screen::Face && local_owns_screen_) {
      const bool steps_changed = local_state().steps != steps_before;
      if (steps_changed) {
        // Steps live in a different band and a full paint covers both. Mark,
        // do not paint: tick() has more than one reason to repaint — this and
        // poll_tilt() below — and painting inline meant one tick could run TWO
        // full-face renders back to back (phase 8 at 466 ms against a 234 ms
        // render, almost exactly double). app_loop drains the flag once per
        // iteration and coalesces, as on_link_up has always done.
        last_painted_minute_ = minute_now;
        mark_paint_pending();
      } else if (minute_now != last_painted_minute_) {
        // Only the digits changed. Painted inline rather than coalesced
        // because a band is ~5 tile passes: the reason to defer a repaint is
        // that it is expensive, and this one is not.
        last_painted_minute_ = minute_now;
        show_clock_band();
      }
    }
  }
  if (now_ms - last_batt_ms_ >= 1000u) {
    poll_battery(now_ms);
    last_batt_ms_ = now_ms;
  }
  poll_alarms();

  // Sleep last, so anything above that counted as activity has already said so.
  // Deliberately NOT gated on updating_: an OTA drives the panel through
  // show_ota_progress() and calls wake_display() with every step, which keeps
  // the timeout pushed forward for as long as the transfer is alive.
  const std::uint32_t timeout = sleep_timeout_ms();
  if (power_ == Power::Running && timeout != 0u &&
      static_cast<std::uint32_t>(now_ms - last_activity_ms_) >= timeout) {
    enter_sleep();
  }

  // wake_until comparison is unused today; if revived, use (now - wake) with
  // unsigned elapsed, not raw >= across a u32 wrap.
  if (display_on_ && wake_until_ms_ != 0u && now_ms >= wake_until_ms_) {
    // Auto-dim handled by caller setting wake_until; keep simple for M10.
  }
  (void)wake_until_ms_;
}

void Core::on_button_press() {
  wake_display();
  // Cycle local screens when showing local UI.
  if (local_state().screen == local::Screen::Alert) {
    local_state().screen = local::Screen::Face;
    show_current();
    return;
  }
  switch (local_state().screen) {
    case local::Screen::Face:
      local_state().screen = local::Screen::Notifs;
      break;
    case local::Screen::Notifs:
      local_state().screen = local::Screen::Settings;
      break;
    case local::Screen::Settings:
      local_state().screen = local::Screen::Face;
      break;
    case local::Screen::Charging:
      local_state().screen = local::Screen::Face;
      break;
    default:
      local_state().screen = local::Screen::Face;
      break;
  }
  show_current();
}

bool Core::on_tap_elem(std::uint16_t elem_id) {
  wake_display();
  if (local_state().screen != local::Screen::Settings) {
    return false;
  }
  bool changed = true;
  switch (elem_id) {
    case local::kSettingRaise:
      local_state().settings.tilt_enabled =
          local_state().settings.tilt_enabled ? 0u : 1u;
      break;
    case local::kSettingTimeout: {
      // Cycle the values a person would actually pick, rather than stepping by
      // one second through a 5..120 range nobody wants to tap through.
      static constexpr std::uint8_t kChoices[] = {10u, 20u, 30u, 60u, 120u, 0u};
      const std::uint8_t current = local_state().settings.wake_seconds;
      std::size_t idx = 0u;
      for (std::size_t i = 0u; i < sizeof(kChoices); ++i) {
        if (kChoices[i] == current) {
          idx = i;
          break;
        }
      }
      local_state().settings.wake_seconds =
          kChoices[(idx + 1u) % sizeof(kChoices)];
      break;
    }
    case local::kSettingSteps:
      local_state().settings.face_show_steps =
          local_state().settings.face_show_steps ? 0u : 1u;
      break;
    default:
      changed = false;
      break;
  }
  if (!changed) {
    return false;
  }
  // A local edit is a new revision, and the phone must be told. Bumping past
  // anything either side has shown is what makes "last writer wins" hold after
  // both have been offline - see settings_sync::next_revision.
  local_state().settings.revision = settings_sync::next_revision(
      local_state().settings.revision, highest_seen_rev_);
  highest_seen_rev_ = local_state().settings.revision;
  save_settings();
  settings_dirty_ = true;
  show_current();
  return true;
}

bool Core::take_settings_dirty() {
  const bool d = settings_dirty_;
  settings_dirty_ = false;
  return d;
}

settings_sync::Payload Core::settings_payload() const {
  return settings_sync::from(local_state().settings,
                             local_state().settings.revision);
}

bool Core::apply_settings_sync(const settings_sync::Payload& incoming) {
  if (incoming.revision > highest_seen_rev_) {
    highest_seen_rev_ = incoming.revision;
  }
  const settings_sync::Payload mine = settings_payload();
  // self_is_watch = true: on a tie the watch keeps its own, and the phone runs
  // the same rule from its side and yields. Both reach that verdict alone,
  // which is what stops the two overwriting each other forever.
  if (!settings_sync::should_apply(mine, incoming, /*self_is_watch=*/true)) {
    return false;
  }
  settings_sync::apply_to(incoming, &local_state().settings);
  local_state().settings.revision = incoming.revision;
  save_settings();
  show_current();
  return true;
}

}  // namespace core
}  // namespace slate
