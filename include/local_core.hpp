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
  void set_remote_stale(bool stale);

  void on_system_message(const std::uint8_t* msg, std::size_t len);
  void apply_cts_time(std::uint32_t unix_epoch_sec);

  // Call ~200–500 ms. Handles alarms, steps, tilt wake, charging screen, redraw.
  void tick(std::uint32_t now_ms);

  // Local navigation when remote_depth==0.
  void on_button_press();
  void on_tap_elem(std::uint16_t elem_id);

  void show_current();
  void wake_display();

private:
  void poll_steps();
  void poll_battery();
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
  std::uint8_t dl_buf_[512] = {};
  std::uint32_t wake_until_ms_ = 0u;
  std::uint32_t last_step_ms_ = 0u;
  std::uint32_t last_batt_ms_ = 0u;
  bool display_on_ = true;
};

}  // namespace core
}  // namespace slate
