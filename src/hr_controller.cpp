#include "hr_controller.hpp"

#include "hr_task.hpp"

namespace slate {
namespace hr {

void Controller::enable() {
  if (task_ != nullptr) {
    state_ = State::NotEnoughData;
    task_->push(Task::Msg::Enable);
  }
}

void Controller::disable() {
  if (task_ != nullptr) {
    state_ = State::Stopped;
    task_->push(Task::Msg::Disable);
  }
  heart_rate_ = 0u;
}

void Controller::update(State new_state, std::uint8_t heart_rate) {
  state_ = new_state;
  if (heart_rate_ != heart_rate) {
    heart_rate_ = heart_rate;
    if (notify_ != nullptr) {
      notify_(heart_rate, notify_ctx_);
    }
  }
}

}  // namespace hr
}  // namespace slate
