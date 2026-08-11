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
  /** Notif detail body (null / empty while pending). */
  const char* detail_text = nullptr;
  bool detail_pending = false;
};

// Returns bytes written (0 on overflow / null). Includes COMMIT.
std::size_t build_screen(const ViewModel& vm, std::uint8_t* out, std::size_t cap);

/**
 * Band-only "UPDATING nn%" overlay drawn during a firmware transfer.
 * Emits no CLEAR, so it repaints a single band rather than the whole panel.
 */
std::size_t build_ota_banner(std::uint8_t* out, std::size_t cap,
                             std::uint8_t pct);

/**
 * HH:MM only, as a band (y 56..95), for the once-a-minute rollover.
 * Emits no CLEAR, so untouched tiles are culled.
 */
std::size_t build_clock_band(std::uint8_t* out, std::size_t cap,
                             std::uint16_t face_bright);

#if SLATE_DIAG_OVERLAY
/**
 * The three diagnostic lines only, as a band (y 16..51).
 *
 * The periodic overlay refresh used to repaint the entire face — ~238 ms of
 * full-screen SPI every 2 s, on the task that also drains the SDP inbox
 * (N-36). Emits no CLEAR so untouched tiles are culled.
 */
std::size_t build_diag_banner(std::uint8_t* out, std::size_t cap,
                              const local::State& st);
#endif

}  // namespace ui
}  // namespace slate
