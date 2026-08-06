#pragma once

#include "alarm_sched.hpp"
#include "bma42x.hpp"
#include "local_state.hpp"
#include "notif_store.hpp"

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
  void* ctx = nullptr;
};

struct BudgetReport {
  std::size_t local_state_bytes = 0u;
  std::size_t local_block_bytes = 0u;
  std::size_t local_budget = 0u;
  std::size_t notif_store_bytes = 0u;
  std::size_t notif_budget = 0u;
};

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
  void on_tap_elem(std::uint16_t elem_id);

  /** False while a phone-pushed screen owns the panel; blocks local repaints. */
  void set_local_owns_screen(bool owns) { local_owns_screen_ = owns; }

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
  void wake_display();

private:
  void poll_steps();
  void poll_battery(std::uint32_t now_ms);
  void poll_alarms();
  void poll_tilt();
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
  std::uint8_t dl_buf_[512] = {};
  std::uint32_t wake_until_ms_ = 0u;
  std::uint32_t last_step_ms_ = 0u;
  std::uint32_t last_batt_ms_ = 0u;
  // Minute shown by the last face repaint; 0xFF forces the first paint.
  std::uint8_t last_painted_minute_ = 0xFFu;
  bool paint_pending_ = false;
  std::uint32_t last_adc_ms_ = 0u;
  bool display_on_ = true;
};

}  // namespace core
}  // namespace slate
