#pragma once

#include "hr_controller.hpp"
#include "hr_task.hpp"
#include "hrs3300.hpp"

#include <cstdint>

// Device-side session gate: settings hr_enabled turns measuring on/off.
// CCCD only controls GATT notifications of the same stream.

namespace slate {
namespace hr {

struct Session {
  hrs::Driver* sensor = nullptr;
  Controller* controller = nullptr;
  Task* task = nullptr;
  bool hr_enabled = false;
};

inline void refresh(Session& s) {
  if (s.controller == nullptr || s.task == nullptr) {
    return;
  }
  // Continuous while the settings gate is On (user opted in to the cost).
  s.task->set_local_session(false);
  if (s.hr_enabled) {
    if (s.controller->state() == State::Stopped) {
      s.controller->enable();
    }
  } else {
    if (s.controller->state() != State::Stopped) {
      s.controller->disable();
    }
  }
}

}  // namespace hr
}  // namespace slate
