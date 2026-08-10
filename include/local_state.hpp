#pragma once

#include "local_budgets.hpp"

#include <cstddef>
#include <cstdint>

// Aggregated local UI / sensor / settings state — fits kLocalScreenStateBytes.

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
  /**
   * Seconds of inactivity before the display sleeps. 0 disables sleeping;
   * Core clamps the rest to 5..120.
   *
   * 20 rather than the old 5: five seconds is long enough to bench-test the
   * sleep path and far too short to read anything, which was fine while nothing
   * used the value and is not now.
   */
  std::uint8_t wake_seconds = 20u;
  std::uint8_t face_show_steps = 1u;
  /** Bring-up diagnostic lines on the watch face (and the 2 s band refresh). */
  std::uint8_t face_show_diag = 1u;
  /**
   * Master gate for HRS3300. Off = sensor stays asleep; On measures continuously
   * and shows BPM on the watch face (graph/history belongs in a JS sub-app).
   */
  std::uint8_t hr_enabled = 0u;
  /**
   * Sync revision, persisted with the values it stamps.
   *
   * Held here rather than only in Core because it has to survive a reboot. Kept
   * in RAM alone it returned to 0 on every restart, and the phone — still
   * holding a revision of its own — then outranked the watch on the opening
   * exchange and quietly put its older copy back. That is precisely the failure
   * the counter exists to prevent, and a watch reboots often.
   *
   * Not part of settings_sync::Payload's apply path: it is metadata about the
   * settings, not a setting.
   */
  std::uint32_t revision = 0u;
  std::uint32_t magic = 0u;
};

// Bumped 8 Aug 2026 ('SLTS' -> 'SLTT') so the new 20 s display timeout actually
// reaches watches that already have a settings block persisted with the old
// 5 s default. load_settings() only applies defaults when the magic mismatches,
// so without a bump the change would silently do nothing on this very watch.
// Cost: any customised tilt settings revert to defaults once. Acceptable: the
// watch settings UI did not exist when SLTT shipped, so nothing useful was lost.
//
// Bumped again 9 Aug 2026 ('SLTT' -> 'SLTU'): `revision` was added, so the
// struct is a different size and shape. An old blob read into the new layout
// would land a stale magic and be rejected anyway, but the bump makes that
// explicit rather than incidental.
/**
 * Element ids on the settings screen — the contract between build_settings()
 * and Core::on_tap_elem(). Named here so the drawing and the handling cannot
 * drift apart silently, which is the sort of bug that presents as "tapping
 * that row does nothing".
 */
constexpr std::uint16_t kSettingRaise = 1u;
constexpr std::uint16_t kSettingTimeout = 2u;
constexpr std::uint16_t kSettingSteps = 3u;
constexpr std::uint16_t kSettingDiag = 4u;
constexpr std::uint16_t kSettingHr = 5u;

// Bumped 10 Aug 2026 ('SLTV' -> 'SLTW'): hr_enabled added.
constexpr std::uint32_t kSettingsMagic = 0x534C5457u;  // 'SLTW'

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
  /** Stall episodes >=100 ms since boot. Frequency, where stall_ms is depth. */
  std::uint32_t diag_stall_events = 0u;
  // BLE bring-up checkpoint + failing rc (ble::bringup_snapshot state map:
  // 7 = advertising, 9x = failure at stage x). Sealed units have no SWD, so
  // the overlay is the only way to see where the radio died.
  std::uint8_t diag_ble_state = 0u;
  std::uint16_t diag_ble_rc = 0u;
  // FreeRTOS ticks recovered by the RTC1 catch-up (N-10). 0 = the tick never
  // fell behind COUNTER; a climbing value is the time that used to be lost
  // silently while the radio held off the tick ISR.
  std::uint32_t diag_tick_catchup = 0u;
  // Slowest app_loop phase since boot and how long it took (N-13 bring-up).
  // Ids: 1 inbox drain, 2 input poll, 3 session.tick, 4 diag band repaint,
  // 5 battery/BAS, 6 notify wait, 7 link transition, 8 core.tick.
  // 3 and 8 were one phase until N-36 needed them apart.
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
  /**
   * BMA identity and the result of the feature-config upload.
   *
   * `diag_bma_status` is the sensor's INTERNAL_STATUS (0x2A) after the 6 KB
   * Bosch stream: 1 means the feature ASIC booted and the pedometer is live,
   * anything else means it did not. Added because "steps read 0" is
   * indistinguishable between "the blob never loaded" and "you have not walked",
   * and one flash spent making the sensor say which is cheaper than guessing.
   */
  std::uint8_t diag_bma_chip = 0u;
  std::uint8_t diag_bma_status = 0xFFu;
  /**
   * 1 once the pedometer enable bit has been written AND read back verified.
   *
   * Separate from `diag_bma_status` because they fail independently: the config
   * stream can load perfectly (status 1) while the feature write lands at the
   * wrong ASIC address and the pedometer stays off. That is exactly the state
   * the watch was in on 8 Aug.
   */
  std::uint8_t diag_bma_step_en = 0u;
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

  /**
   * Raise-to-wake bring-up (line 3 of the face diag overlay).
   *
   * Format: `<sleeps>.<samples>/<rej>.<fires>.<wake>/<x>/<y>/<z>`
   *   sleeps  enter_sleep count since boot
   *   samples accel polls during the last sleep (0 = never slept, or starved)
   *   rej     last RaiseDetector::reject_code while history was full
   *           (0 fire, 1 filling, 2 |x|, 3 y-var, 4 face-down, 5 y-mean, 6 roll)
   *   fires   raise-to-wake successes
   *   wake    last wake source: 0 none, 1 raise, 2 button, 3 double-tap, 4 charge
   *   x/y/z   last accel sample (mount frame, 1g ≈ 1024)
   */
  std::uint16_t diag_sleep_enters = 0u;
  std::uint16_t diag_raise_samples = 0u;
  std::uint8_t diag_raise_reject = 0u;
  std::uint8_t diag_raise_fires = 0u;
  std::uint8_t diag_wake_src = 0u;
  std::int16_t diag_ax = 0;
  std::int16_t diag_ay = 0;
  std::int16_t diag_az = 0;

  std::uint32_t steps = 0u;
  std::uint32_t steps_at_day_start = 0u;
  std::uint32_t day_epoch = 0u;  // unix midnight of current local day

  Settings settings{};

  /** Last BPM from the HR task (0 when unknown / stopped / disabled). */
  std::uint8_t hr_bpm = 0u;
  /**
   * 0 stopped, 1 waiting for data, 2 running, 3 off-wrist / ambient.
   * Mirrors slate::hr::State for the face BPM without pulling that header
   * into every local_ui consumer.
   */
  std::uint8_t hr_status = 0u;

  // Alert screen payload (alarm/timer).
  std::uint8_t alert_kind = 0u;  // 1=alarm 2=timer
  std::uint8_t alert_id = 0u;
  char alert_label[20] = {};
};

// Local screen state + a small reserve. Sized to kLocalScreenStateBytes (I-19).
struct ScreenBlock {
  State state{};
  std::uint8_t reserve[budget::kLocalScreenStateBytes - sizeof(State)] = {};
};

static_assert(sizeof(ScreenBlock) == budget::kLocalScreenStateBytes,
              "local screen block must match kLocalScreenStateBytes");
static_assert(sizeof(State) <= budget::kLocalScreenStateBytes,
              "local State overflows screen-block budget");

constexpr std::size_t state_bytes_used() { return sizeof(State); }
constexpr std::size_t screen_block_bytes() { return sizeof(ScreenBlock); }

}  // namespace local
}  // namespace slate
