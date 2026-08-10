#pragma once

#include <cstddef>
#include <cstdint>

namespace slate {
namespace hr {

class Task;

enum class State : std::uint8_t {
  Stopped = 0,
  NotEnoughData = 1,
  NoTouch = 2,
  Running = 3,
};

/** Encodes BLE HRS Measurement (flags + u8 BPM). Returns 2. */
inline std::size_t encode_measurement(std::uint8_t bpm, std::uint8_t out[2]) {
  out[0] = 0u;  // flags: UINT8 BPM, no sensor contact bit
  out[1] = bpm;
  return 2u;
}

class Controller {
public:
  using NotifyFn = void (*)(std::uint8_t bpm, void* ctx);

  void set_task(Task* task) { task_ = task; }
  void set_notify(NotifyFn fn, void* ctx) {
    notify_ = fn;
    notify_ctx_ = ctx;
  }

  void enable();
  void disable();
  void update(State new_state, std::uint8_t heart_rate);

  State state() const { return state_; }
  std::uint8_t heart_rate() const { return heart_rate_; }

private:
  Task* task_ = nullptr;
  NotifyFn notify_ = nullptr;
  void* notify_ctx_ = nullptr;
  State state_ = State::Stopped;
  std::uint8_t heart_rate_ = 0u;
};

}  // namespace hr
}  // namespace slate
