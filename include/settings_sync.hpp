#pragma once

#include "local_state.hpp"

#include <cstddef>
#include <cstdint>

// Bidirectional settings sync, watch <-> phone.
//
// Pure encode/decode plus the merge rule, deliberately free of BLE, Core and
// FreeRTOS so the whole conflict-resolution question can be settled on the
// desktop. "Whichever side changed last wins" is easy to say and easy to get
// subtly wrong, and the failure mode is a user's setting silently reverting.

namespace slate {
namespace settings_sync {

constexpr std::uint8_t kWireVersion = 4u;
/**
 * op + version + u32 revision + tilt + wake + steps + diag + hr + 3×u16 RGB565
 * + raise_sens + shake_enabled + shake_sens.
 */
constexpr std::size_t kPayloadBytes = 20u;

/**
 * The synced subset of local::Settings.
 *
 * Legacy `tilt_sensitivity` (BMA any-motion) is deliberately absent: it does
 * nothing for raise/shake. Raise and shake each have Soft/Normal/Hard.
 */
struct Payload {
  std::uint32_t revision = 0u;
  std::uint8_t tilt_enabled = 1u;
  std::uint8_t wake_seconds = 20u;
  std::uint8_t face_show_steps = 1u;
  /** Watch-face diagnostic overlay lines (bring-up counters). */
  std::uint8_t face_show_diag = 1u;
  /**
   * Master gate for the HRS3300. Off keeps the sensor asleep; On measures
   * continuously and shows BPM on the watch face.
   */
  std::uint8_t hr_enabled = 0u;
  std::uint16_t ui_chrome = 0xFFFFu;
  std::uint16_t face_bright = 0xFFFFu;
  std::uint16_t face_dim = 0x8410u;
  /** Soft=0 / Normal=1 / Hard=2. */
  std::uint8_t raise_sensitivity = 1u;
  std::uint8_t shake_enabled = 0u;
  /** Soft=0 / Normal=1 / Hard=2. */
  std::uint8_t shake_sensitivity = 1u;
};

/** Writes exactly [kPayloadBytes]. Returns 0 if `cap` is too small. */
std::size_t encode(const Payload& p, std::uint8_t* out, std::size_t cap);

/** False on a short buffer, wrong opcode or an unknown wire version. */
bool decode(const std::uint8_t* msg, std::size_t len, Payload* out);

/** True when the two carry different settings, revision aside. */
bool differs(const Payload& a, const Payload& b);

/**
 * Should `incoming` replace `current`?
 *
 * Higher revision wins. Equal revisions with equal content is a no-op. Equal
 * revisions with DIFFERENT content is a genuine conflict — both sides edited
 * without seeing each other — and [watch_wins_ties] decides, so the two ends
 * reach the same answer independently rather than ping-ponging updates at each
 * other forever.
 */
bool should_apply(const Payload& current, const Payload& incoming,
                  bool self_is_watch);

/**
 * Revision to stamp on a local edit: one past anything either side has shown.
 *
 * This is what makes the counter a Lamport clock rather than a sequence
 * number, and it is the whole reason last-writer-wins holds after both sides
 * have been offline and edited.
 */
std::uint32_t next_revision(std::uint32_t local_rev, std::uint32_t highest_seen);

/** Copy the synced fields into a local::Settings, leaving the rest alone. */
void apply_to(const Payload& p, local::Settings* s);

/** Snapshot the synced fields out of a local::Settings. */
Payload from(const local::Settings& s, std::uint32_t revision);

}  // namespace settings_sync
}  // namespace slate
