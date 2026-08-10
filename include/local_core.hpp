#pragma once

#include "alarm_sched.hpp"
#include "bma42x.hpp"
#include "local_state.hpp"
#include "notif_store.hpp"
#include "raise_wake.hpp"
#include "settings_sync.hpp"

#include <cstddef>
#include <cstdint>

// Resilient core orchestrator — local screens, sensors, alarms, notif store.

namespace slate {
namespace core {

struct Hooks {
  // Push a local display list (replaces remote when depth==0 or forced).
  void (*push_list)(const std::uint8_t* data, std::size_t len, void* ctx) = nullptr;
  void (*haptic)(std::uint8_t pattern, void* ctx) = nullptr;
  void (*backlight)(std::uint8_t percent, void* ctx) = nullptr;
  // Optional: SAADC sample into battery_hw (device). Host leaves null.
  void (*sample_battery)(void* ctx) = nullptr;
  /**
   * Put the panel into or out of sleep (ST7789 SLPIN/SLPOUT).
   *
   * A hook rather than a direct call so Core stays free of driver headers and
   * the host tests keep building. Null on host; the device wires it in main.
   */
  void (*display_sleep)(bool sleep, void* ctx) = nullptr;
  /**
   * Reconcile HR enable sources after settings / local HR UI / sleep changes.
   * Device wires this to slate::hr::refresh; host tests leave null.
   */
  void (*hr_refresh)(void* ctx) = nullptr;
  void* ctx = nullptr;
};

struct BudgetReport {
  std::size_t local_state_bytes = 0u;
  std::size_t local_block_bytes = 0u;
  std::size_t local_budget = 0u;
  std::size_t notif_store_bytes = 0u;
  std::size_t notif_budget = 0u;
};

/**
 * Bounds on the inactivity timeout.
 *
 * The floor stops a stored 1 s from making the watch unusable; the ceiling
 * stops a stored value leaving the panel lit for minutes, which is the whole
 * cost this feature exists to remove.
 */
/**
 * Accelerometer poll period for raise-to-wake, matching InfiniTime's 100 ms
 * `stateUpdatePeriod`. The 8-sample history is therefore 0.8 s of movement.
 */
constexpr std::uint32_t kRaisePollMs = 100u;

constexpr std::uint32_t kMinSleepSeconds = 5u;
constexpr std::uint32_t kMaxSleepSeconds = 120u;

class Core {
public:
  void init(const Hooks& hooks, bma::Driver* bma);

  local::State& local_state() { return block_.state; }
  const local::State& local_state() const { return block_.state; }
  notif::Store& notifs() { return notifs_; }
  alarm::Table& alarms() { return alarms_; }

  BudgetReport budgets() const;

  void on_link_up();
  void on_link_down();

  /** True once since the last call if the face needs a deferred repaint. */
  bool take_paint_pending();
  void mark_paint_pending() { paint_pending_ = true; }
  void set_remote_stale(bool stale);

  void on_system_message(const std::uint8_t* msg, std::size_t len);
  void apply_cts_time(std::uint32_t unix_epoch_sec);

  // Call ~200–500 ms. Handles alarms, steps, tilt wake, charging screen, redraw.
  void tick(std::uint32_t now_ms);

  // Local navigation when remote_depth==0.
  void on_button_press();

  /**
   * A tap on an element of a locally-drawn screen.
   *
   * Returns true when the watch acted on it, so the caller knows not to also
   * forward the event to the phone. The screen check lives here rather than at
   * the call site: only Core knows which of its screens have live elements, and
   * a phone that has never heard of these element ids should not be sent them.
   */
  bool on_tap_elem(std::uint16_t elem_id);

  /** False while a phone-pushed screen owns the panel; blocks local repaints. */
  void set_local_owns_screen(bool owns) {
    // Ownership changing invalidates what Core believes is on the panel: while
    // the phone held it, the phone was pushing lists Core never saw. Forgetting
    // the digest here is what stops the repaint suppression in show_current()
    // from skipping the paint that brings the local face back.
    if (owns != local_owns_screen_) {
      forget_pushed_digest();
    }
    local_owns_screen_ = owns;
  }

  /** True while firmware is being received; blocks local repaints entirely. */
  void set_updating(bool updating) { updating_ = updating; }

  void show_current();

  /**
   * Swipe-to-launcher with no phone attached.
   *
   * The app drawer is composed on the phone — the watch has no idea what is
   * installed — so with the link down there is nothing to push and the gesture
   * would otherwise do nothing at all. Say so instead. Cleared automatically
   * on the next link-up (on_link_up sends Disconnected back to Face).
   */
  void show_not_connected();

  /** Band-only "UPDATING nn%" repaint, safe to call during a transfer. */
  void show_ota_progress(std::uint8_t pct);

  /**
   * Repaint just the diagnostic band, not the whole face.
   *
   * For the periodic overlay refresh. Everything else still goes through
   * show_current(), which owns the guard about when the local face may paint.
   */
  void show_diag_band();

  /** Repaint HH:MM only. For the minute rollover; ~5 tile passes, not 30. */
  void show_clock_band();
  /**
   * Wake the panel and restart the inactivity timeout.
   *
   * The single wake entry point, deliberately: every source already funnels
   * here — button, tilt, touch, charge edge, alert — so making this the place
   * that leaves sleep means a new source cannot forget to. Same argument as
   * the guard inside show_current().
   *
   * [src] is latched onto diag_wake_src when leaving sleep: 1 raise, 2 button,
   * 3 double-tap, 4 charge, 5 alert. 0 leaves the previous value.
   */
  void wake_display(std::uint8_t src = 0u);

  /** True while the panel is asleep. Nothing local paints in this state. */
  bool sleeping() const { return power_ == Power::Sleeping; }

  /**
   * Record user activity without waking. Used by paths that should defer sleep
   * but not light the screen on their own.
   */
  void note_activity() { last_activity_ms_ = last_tick_ms_; activity_seeded_ = true; }

  /** Seconds of inactivity before the panel sleeps; 0 disables sleeping. */
  std::uint32_t sleep_timeout_ms() const;

  /**
   * Raise-to-wake sampling. Call from the app loop on EVERY iteration.
   *
   * Deliberately not driven from tick(): tick runs every 200 ms, so the
   * internal 100 ms gate could never reach 100 Hz and the 8-sample window
   * stretched to 1.6 s. A wrist raise takes about half a second, so it fell
   * entirely inside one window and the "now versus 0.8 s ago" comparison the
   * algorithm depends on washed out completely. That is why the flick did
   * nothing on hardware.
   */
  void poll_raise(std::uint32_t now_ms);

  /**
   * True once since the last call if a local settings edit needs sending.
   *
   * Pull rather than push: Core has no link and should not grow one, and the
   * app loop already drains state on its own schedule.
   */
  bool take_settings_dirty();

  /** Current settings plus this watch's revision, ready to encode. */
  settings_sync::Payload settings_payload() const;

  /**
   * Merge a settings message from the phone. Returns true if it was applied.
   *
   * Also records the highest revision seen, which is what a later local edit
   * counts up from.
   */
  bool apply_settings_sync(const settings_sync::Payload& incoming);

  /** Open the settings screen (swipe left-to-right from the face). */
  void show_settings() {
    wake_display();
    local_state().screen = local::Screen::Settings;
    show_current();
  }

  /** Leave settings (or any local screen) back to the watch face. */
  void show_face() {
    wake_display();
    local_state().screen = local::Screen::Face;
    show_current();
  }

private:
  void poll_steps();
  void poll_battery(std::uint32_t now_ms);
  void poll_alarms();
  void enter_sleep();
  void enter_alert(std::uint8_t kind, std::uint8_t id);
  void load_settings();
  void save_settings();

  Hooks hooks_{};
  bma::Driver* bma_ = nullptr;
  local::ScreenBlock block_{};
  notif::Store notifs_{};
  alarm::Table alarms_{};
  bool local_owns_screen_ = true;
  bool updating_ = false;

  /**
   * Display power state.
   *
   * Two states, not InfiniTime's four. InfiniTime needs `GoingToSleep` because
   * `DisplayApp` and `SystemTask` are separate tasks and the handover is
   * asynchronous — the panel has to finish sleeping before peripherals can be
   * powered down, and a wake can arrive mid-flight. Slate does all of this on
   * the one app task, so the transition cannot be interrupted and the extra
   * states would carry no information. `AODSleeping` needs an always-on
   * display mode Slate does not have.
   */
  enum class Power : std::uint8_t { Running, Sleeping };
  Power power_ = Power::Running;

  /** Last input, alert or charge event — what the sleep timeout counts from. */
  std::uint32_t last_activity_ms_ = 0u;
  /** Latest tick, so wake_display() can stamp activity without a parameter. */
  std::uint32_t last_tick_ms_ = 0u;
  bool activity_seeded_ = false;
  std::uint8_t dl_buf_[512] = {};
  std::uint32_t wake_until_ms_ = 0u;
  std::uint32_t last_step_ms_ = 0u;
  std::uint32_t last_batt_ms_ = 0u;
  // Minute shown by the last face repaint; 0xFF forces the first paint.
  std::uint8_t last_painted_minute_ = 0xFFu;
  bool paint_pending_ = false;

  /**
   * Digest of the last display list Core pushed, so an unchanged screen is not
   * repainted.
   *
   * A digest rather than a copy of the bytes: `dl_buf_` is 512 B and the link
   * map leaves ~432 B between `__heap_end__` and `__StackLimit`, so a second
   * buffer does not fit. Four bytes does.
   *
   * Zero means "unknown" and always repaints. It must be reset whenever
   * anything other than this path writes the panel — a band, the OTA banner,
   * or the phone taking the screen — or a later "nothing changed" would leave
   * that other thing on display forever.
   */
  std::uint32_t pushed_digest_ = 0u;

  void forget_pushed_digest() { pushed_digest_ = 0u; }

  /** FNV-1a. Cheap, and collisions only ever cost one skipped repaint. */
  static std::uint32_t digest_of(const std::uint8_t* p, std::size_t n) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0u; i < n; ++i) {
      h ^= p[i];
      h *= 16777619u;
    }
    // Never collide with the "unknown" sentinel.
    return h == 0u ? 1u : h;
  }
  std::uint32_t last_adc_ms_ = 0u;

  /**
   * Raise-to-wake. Polled only while asleep, so it costs nothing when the
   * screen is already on.
   */
  motion::RaiseDetector raise_{};
  std::uint32_t last_raise_ms_ = 0u;

  /**
   * Highest revision either side has shown, this boot.
   *
   * Only the floor for the next local edit, so losing it on reboot is harmless:
   * load_settings() seeds it from the persisted revision, which is the correct
   * floor. The revision itself lives in local::Settings because that one *must*
   * survive a restart.
   */
  std::uint32_t highest_seen_rev_ = 0u;
  bool settings_dirty_ = false;
  bool display_on_ = true;
};

}  // namespace core
}  // namespace slate
