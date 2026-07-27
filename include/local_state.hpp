#pragma once

#include "local_budgets.hpp"

#include <cstddef>
#include <cstdint>

// Aggregated local UI / sensor / settings state — must fit §3.2 3 KB budget.

namespace slate {
namespace local {

enum class Screen : std::uint8_t {
  Face = 0,
  Notifs,
  Settings,
  Charging,
  Alert,
  Disconnected,
};

struct Settings {
  std::uint8_t tilt_enabled = 1u;
  std::uint8_t tilt_sensitivity = 3u;  // 0=soft … 7=hard (any-motion threshold)
  std::uint8_t wake_seconds = 5u;
  std::uint8_t face_show_steps = 1u;
  std::uint32_t magic = 0u;
};

constexpr std::uint32_t kSettingsMagic = 0x534C5453u;  // 'SLTS'

struct State {
  Screen screen = Screen::Face;
  std::uint8_t notif_sel = 0u;
  std::uint8_t settings_sel = 0u;
  std::uint8_t charging = 0u;
  std::uint8_t link_up = 0u;
  std::uint8_t remote_stale = 0u;  // session heartbeat overlay
  std::uint8_t battery_pct = 100u;
  std::uint8_t pad0 = 0u;

  std::uint32_t steps = 0u;
  std::uint32_t steps_at_day_start = 0u;
  std::uint32_t day_epoch = 0u;  // unix midnight of current local day

  Settings settings{};

  // Alert screen payload (alarm/timer).
  std::uint8_t alert_kind = 0u;  // 1=alarm 2=timer
  std::uint8_t alert_id = 0u;
  char alert_label[20] = {};

  // Scratch for built display lists (shared; not counted separately from budget
  // when we place the builder buffer elsewhere — see local_ui).
  std::uint8_t pad1[8] = {};
};

// Full resilient-core RAM block reported against the 3 KB budget.
struct ScreenBlock {
  State state{};
  // Room for future face caches / layout ids without blowing the budget.
  std::uint8_t reserve[budget::kLocalScreenStateBytes - sizeof(State)] = {};
};

static_assert(sizeof(ScreenBlock) == budget::kLocalScreenStateBytes,
              "local screen block must be exactly 3 KB");
static_assert(sizeof(State) <= budget::kLocalScreenStateBytes,
              "local State overflows 3 KB");

constexpr std::size_t state_bytes_used() { return sizeof(State); }
constexpr std::size_t screen_block_bytes() { return sizeof(ScreenBlock); }

}  // namespace local
}  // namespace slate
