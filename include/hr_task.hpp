#pragma once

#include "hrs3300.hpp"
#include "hr_controller.hpp"
#include "ppg.hpp"

#include <cstdint>

namespace slate {
namespace hr {

/**
 * FreeRTOS heart-rate worker (InfiniTime HeartRateTask, simplified).
 *
 * Measuring only while Enable is held; no background intervals.
 * Soft give-up after ~30 s with no BPM when local-only.
 */
class Task {
public:
  enum class Msg : std::uint8_t {
    Enable = 0,
    Disable = 1,
    GoToSleep = 2,
    WakeUp = 3,
  };

  Task(hrs::Driver& sensor, Controller& controller);

  /** Create queue + FreeRTOS task. Call once after scheduler is ready. */
  void start();

  void push(Msg msg);
  /** From ISR-safe contexts when needed. */
  void push_from_isr(Msg msg);

  bool measuring() const { return state_ == States::Measuring; }

  /**
   * When true, a GoToSleep stops measurement (local UI session).
   * When false (CCCD subscribed), display sleep keeps measuring.
   */
  void set_local_session(bool local) { local_session_ = local; }
  bool local_session() const { return local_session_; }

private:
  enum class States : std::uint8_t { Disabled, Measuring };

  static void process(void* instance);
  void work();
  void handle_sensor();
  void start_measurement();
  void stop_measurement();

  hrs::Driver& sensor_;
  Controller& controller_;
  Ppg ppg_{};
  void* queue_ = nullptr;
  void* task_handle_ = nullptr;
  States state_ = States::Disabled;
  bool local_session_ = false;
  bool value_shown_ = false;
  bool measurement_ok_ = false;
  std::uint32_t measure_start_ms_ = 0u;
  std::uint16_t sample_count_ = 0u;
};

}  // namespace hr
}  // namespace slate
