#include "local_core.hpp"

#include "battery.hpp"
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
  notif::init(&notifs_);
  alarm::init(&alarms_);
  alarm::load(&alarms_);
  load_settings();
  clock::load_persisted();
  if (bma_ != nullptr && bma_->ok()) {
    (void)bma_->configure(nullptr, 0u, block_.state.settings.tilt_sensitivity);
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
    local_state().settings = local::Settings{};
    local_state().settings.magic = local::kSettingsMagic;
    local_state().settings.tilt_enabled = 1u;
    local_state().settings.tilt_sensitivity = 3u;
    local_state().settings.wake_seconds = 5u;
    local_state().settings.face_show_steps = 1u;
  }
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
  ui::ViewModel vm;
  vm.state = &local_state();
  vm.notifs = &notifs_;
  vm.alarms = &alarms_;
  const std::size_t n = ui::build_screen(vm, dl_buf_, sizeof(dl_buf_));
  if (n > 0u) {
    hooks_.push_list(dl_buf_, n, hooks_.ctx);
  }
}

void Core::wake_display() {
  display_on_ = true;
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
  if (chg && !was) {
    wake_display();
    if (local_state().screen == local::Screen::Face) {
      show_current();
    }
  } else if (!chg && was && local_state().screen == local::Screen::Face) {
    show_current();
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

void Core::poll_tilt() {
  if (!local_state().settings.tilt_enabled || bma_ == nullptr) {
    return;
  }
  if (bma_->consume_tilt_irq()) {
    wake_display();
    if (local_state().screen == local::Screen::Face ||
        local_state().screen == local::Screen::Disconnected) {
      // Coalesced with any other repaint this tick — see Core::tick.
      mark_paint_pending();
    }
  }
}

void Core::tick(std::uint32_t now_ms) {
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
  poll_tilt();

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

void Core::on_tap_elem(std::uint16_t elem_id) {
  wake_display();
  if (local_state().screen == local::Screen::Settings) {
    if (elem_id == 1u) {
      local_state().settings.tilt_enabled =
          local_state().settings.tilt_enabled ? 0u : 1u;
    } else if (elem_id == 2u) {
      local_state().settings.tilt_sensitivity =
          static_cast<std::uint8_t>((local_state().settings.tilt_sensitivity + 1u) & 7u);
      if (bma_) {
        bma_->set_tilt_sensitivity(local_state().settings.tilt_sensitivity);
      }
    }
    save_settings();
    show_current();
  }
  (void)elem_id;
}

}  // namespace core
}  // namespace slate
