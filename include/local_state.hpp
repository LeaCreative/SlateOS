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
  std::uint8_t battery_pct = 0xFFu;  // unknown until first valid SAADC (kPercentUnknown)
  // MCUBoot swapped this image in on trial and will revert it on the next reset
  // until it is confirmed. The face shows a marker so the state is not silent.
  std::uint8_t trial_image = 0u;
  // Temporary bring-up diagnostics rendered under the battery gauge: why the
  // previous boot ended, how long this one has lasted, and whether the side
  // button strobe reads as pressed. Remove with SLATE_DIAG_OVERLAY.
  std::uint8_t diag_button = 0u;

  std::uint32_t diag_reset_reason = 0u;
  std::uint32_t diag_uptime_s = 0u;
  std::uint32_t diag_stall_ms = 0u;
  // BLE bring-up checkpoint + failing rc (ble::bringup_snapshot state map:
  // 7 = advertising, 9x = failure at stage x). Sealed units have no SWD, so
  // the overlay is the only way to see where the radio died.
  std::uint8_t diag_ble_state = 0u;
  std::uint16_t diag_ble_rc = 0u;
  // FreeRTOS ticks recovered by the RTC1 catch-up (N-10). 0 = the tick never
  // fell behind COUNTER; a climbing value is the time that used to be lost
  // silently while the radio held off the tick ISR.
  std::uint32_t diag_tick_catchup = 0u;
  // Slowest app_loop phase since boot and how long it took (N-13 bring-up):
  // 1 drain, 2 input, 3 session/core tick, 4 paint, 5 battery/BAS,
  // 6 the notify wait itself (= the task was not scheduled, not slow work).
  std::uint8_t diag_phase = 0u;
  std::uint32_t diag_phase_ms = 0u;
  // Raw SAADC count and converted millivolts, so the battery curve can be
  // checked against a multimeter without SWD (I-5 / N-12).
  std::uint16_t diag_adc_raw = 0u;
  std::uint16_t diag_mv = 0u;
  // Worst single local display-list render and parse, in ms (N-13).
  std::uint32_t diag_render_ms = 0u;
  std::uint32_t diag_parse_ms = 0u;
  // OTA progress line (I-13), rendered only once a transfer has been begun or
  // refused: percent complete, last NAK reason, and how many NAKs.
  // Display lists applied vs rejected by the interpreter. Without this a
  // rejected list is indistinguishable from one that never arrived — the
  // difference between a parser bug and a transport bug (N-28 follow-up).
  std::uint16_t diag_dl_ok = 0u;
  std::uint16_t diag_dl_rej = 0u;
  // Touch events seen by the app loop, and taps that matched a hit rect.
  // "Nothing happens on tap" has three possible causes — no touch at all, a
  // touch that hit nothing, or a hit whose event never reached the phone —
  // and they need different fixes.
  std::uint16_t diag_touch_irq = 0u;
  std::uint16_t diag_touch_readfail = 0u;
  std::uint16_t diag_touch = 0u;
  std::uint16_t diag_touch_hit = 0u;
  // How many times the local face took the screen back from a remote screen,
  // and the last caller to do it. Three fixes have each gated a different
  // path and a fourth still exists; guessing again is not the way to find it.
  std::uint16_t diag_face_restores = 0u;
  std::uint8_t diag_face_last_src = 0u;
  std::uint16_t diag_dl_drop = 0u;
  std::uint8_t diag_sess_state = 0u;
  // Earlier still: frames rejected by the reassembler, and messages dropped
  // because the single-slot inbox was busy.
  std::uint16_t diag_frame_drop = 0u;
  std::uint16_t diag_inbox_drop = 0u;
  std::uint8_t diag_ota_shown = 0u;
  std::uint8_t diag_ota_pct = 0u;
  std::uint8_t diag_ota_nak = 0u;
  std::uint32_t diag_ota_naks = 0u;
  // Repaint counter. Driven by app-loop iterations rather than the clock, so a
  // climbing paint count next to a frozen uptime means the timebase died while
  // the loop lived, and both frozen means the loop itself stopped.
  std::uint32_t diag_paints = 0u;

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
