#pragma once

#include "alarm_sched.hpp"
#include "local_state.hpp"
#include "notif_store.hpp"

#include <cstddef>
#include <cstdint>

// Build local SDP display lists into a caller buffer (font-0 digits via
// TEXT_SCALED for arm's-length legibility).

namespace slate {
namespace ui {

struct ViewModel {
  const local::State* state = nullptr;
  const notif::Store* notifs = nullptr;
  const alarm::Table* alarms = nullptr;
};

// Returns bytes written (0 on overflow / null). Includes COMMIT.
std::size_t build_screen(const ViewModel& vm, std::uint8_t* out, std::size_t cap);

}  // namespace ui
}  // namespace slate
