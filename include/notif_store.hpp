#pragma once

#include "local_budgets.hpp"

#include <cstddef>
#include <cstdint>

// Channel-4 SYSTEM retained notification store. Wire format matches
// companion SystemNotifCodec (UPSERT/REMOVE/CLEAR_ALL).

namespace slate {
namespace notif {

constexpr std::uint8_t kOpUpsert = 0x01u;
constexpr std::uint8_t kOpRemove = 0x02u;
constexpr std::uint8_t kOpClearAll = 0x03u;
constexpr std::uint8_t kFlagOngoing = 1u << 0;
constexpr std::uint8_t kFlagClearable = 1u << 1;

constexpr std::uint8_t kMaxEntries = 16u;
constexpr std::uint8_t kKeyCap = 40u;
constexpr std::uint8_t kTitleCap = 32u;
constexpr std::uint8_t kTextCap = 72u;

struct Entry {
  std::uint8_t key_len = 0u;
  char key[kKeyCap] = {};
  std::uint8_t flags = 0u;
  std::uint8_t category = 0u;
  char monogram = '?';
  std::uint8_t title_len = 0u;
  char title[kTitleCap] = {};
  std::uint8_t text_len = 0u;
  char text[kTextCap] = {};
  std::uint32_t when_sec = 0u;
  std::uint8_t stale = 0u;  // marked on link loss for UI cue
};

struct Store {
  std::uint8_t count = 0u;
  std::uint8_t pad[3] = {};
  Entry entries[kMaxEntries] = {};
};

static_assert(sizeof(Store) <= budget::kNotifStoreBytes,
              "notification store exceeds 4 KB budget");

void init(Store* store);
void clear(Store* store);

// Ingest a complete SYSTEM message. Returns true if store mutated.
bool on_system_message(Store* store, const std::uint8_t* msg, std::size_t len);

void mark_all_stale(Store* store, bool stale);
const Entry* at(const Store* store, std::uint8_t index);
bool remove_at(Store* store, std::uint8_t index);

// Actual sizeof(Store) for reporting.
constexpr std::size_t store_bytes_used() { return sizeof(Store); }

}  // namespace notif
}  // namespace slate
