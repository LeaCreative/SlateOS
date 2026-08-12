#pragma once

#include "local_budgets.hpp"

#include <cstddef>
#include <cstdint>

// Channel-4 SYSTEM retained notification stubs + call alerts.
// Wire format matches companion SystemNotifCodec.

namespace slate {
namespace notif {

constexpr std::uint8_t kOpUpsert = 0x01u;
constexpr std::uint8_t kOpRemove = 0x02u;
constexpr std::uint8_t kOpClearAll = 0x03u;
constexpr std::uint8_t kOpBody = 0x04u;
constexpr std::uint8_t kOpCallAlert = 0x06u;
constexpr std::uint8_t kOpCallEnd = 0x07u;

constexpr std::uint8_t kFlagOngoing = 1u << 0;
constexpr std::uint8_t kFlagClearable = 1u << 1;
/** Phone bulk-sync / reconnect: retain stub without wake or haptic. */
constexpr std::uint8_t kFlagSilent = 1u << 2;

/** Stub rows only (no resident body). Bodies live in Core's detail buffer. */
constexpr std::uint8_t kMaxEntries = 24u;
/** Android sbn.key is often 40–60+ bytes; must match SystemNotifCodec utf8(key, 64). */
constexpr std::uint8_t kKeyCap = 64u;
constexpr std::uint8_t kTitleCap = 32u;
constexpr std::uint8_t kTextCap = 72u;
constexpr std::uint8_t kCallerCap = 32u;

struct Entry {
  std::uint8_t key_len = 0u;
  char key[kKeyCap] = {};
  std::uint8_t flags = 0u;
  std::uint8_t category = 0u;
  char monogram = '?';
  std::uint8_t title_len = 0u;
  char title[kTitleCap] = {};  // notification title (list row)
  std::uint32_t when_sec = 0u;
  std::uint8_t stale = 0u;
};

struct Store {
  std::uint8_t count = 0u;
  std::uint8_t pad[3] = {};
  Entry entries[kMaxEntries] = {};
};

static_assert(sizeof(Store) <= budget::kNotifStoreBytes,
              "notification store exceeds 4 KB budget");

enum class Ingest : std::uint8_t {
  None = 0,
  StubNew,       // UPSERT inserted a new key
  StubUpdate,    // UPSERT replaced existing
  Removed,
  Cleared,
  Body,          // BODY op (detail filled by caller via parse_body)
  CallAlert,     // CALL_ALERT (caller filled via parse_call_alert)
  CallEnd,
};

void init(Store* store);
void clear(Store* store);

/**
 * Ingest a complete SYSTEM message.
 * BODY / CALL_* return the corresponding Ingest without mutating the stub store
 * (except CallEnd which is informational only). Caller must parse payloads.
 */
Ingest on_system_message(Store* store, const std::uint8_t* msg, std::size_t len);

/** BODY: key + text. Returns false if malformed. */
bool parse_body(const std::uint8_t* msg, std::size_t len, char* key_out,
                std::uint8_t key_cap, std::uint8_t* key_len_out, char* text_out,
                std::uint8_t text_cap, std::uint8_t* text_len_out);

/** CALL_ALERT: caller label string. */
bool parse_call_alert(const std::uint8_t* msg, std::size_t len, char* caller_out,
                      std::uint8_t caller_cap, std::uint8_t* caller_len_out);

void mark_all_stale(Store* store, bool stale);
const Entry* at(const Store* store, std::uint8_t index);
bool remove_at(Store* store, std::uint8_t index);
bool remove_key(Store* store, const char* key, std::uint8_t key_len);
std::int16_t find_key(const Store* store, const std::uint8_t* key,
                      std::uint8_t key_len);

constexpr std::size_t store_bytes_used() { return sizeof(Store); }

}  // namespace notif
}  // namespace slate
