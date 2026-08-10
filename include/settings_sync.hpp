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

constexpr std::uint8_t kWireVersion = 1u;
/** op + version + u32 revision + 3 settings + 1 reserved. */
constexpr std::size_t kPayloadBytes = 10u;

/**
 * The synced subset of local::Settings.
 *
 * `tilt_sensitivity` is deliberately absent: it configured the any-motion
 * threshold, which raise-to-wake replaced, so it now does nothing. Syncing a
 * control that has no effect would be worse than not offering it.
 */
struct Payload {
  std::uint32_t revision = 0u;
  std::uint8_t tilt_enabled = 1u;
  std::uint8_t wake_seconds = 20u;
  std::uint8_t face_show_steps = 1u;
  /** Watch-face diagnostic overlay lines (bring-up counters). */
  std::uint8_t face_show_diag = 1u;
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
