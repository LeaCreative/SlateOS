#include "notif_store.hpp"

#include <cstring>

namespace slate {
namespace notif {
namespace {

bool key_eq(const Entry& e, const std::uint8_t* key, std::uint8_t key_len) {
  if (e.key_len != key_len) {
    return false;
  }
  return std::memcmp(e.key, key, key_len) == 0;
}

void copy_str(char* dst, std::uint8_t cap, const std::uint8_t* src, std::uint8_t len,
              std::uint8_t* out_len) {
  if (len > cap) {
    len = cap;
  }
  if (len > 0u) {
    std::memcpy(dst, src, len);
  }
  if (len < cap) {
    dst[len] = '\0';
  }
  *out_len = len;
}

}  // namespace

std::int16_t find_key(const Store* s, const std::uint8_t* key, std::uint8_t key_len) {
  if (s == nullptr || key == nullptr) {
    return -1;
  }
  for (std::uint8_t i = 0u; i < s->count; ++i) {
    if (key_eq(s->entries[i], key, key_len)) {
      return static_cast<std::int16_t>(i);
    }
  }
  return -1;
}

void init(Store* store) {
  if (store == nullptr) {
    return;
  }
  std::memset(store, 0, sizeof(*store));
}

void clear(Store* store) {
  init(store);
}

Ingest on_system_message(Store* store, const std::uint8_t* msg, std::size_t len) {
  if (store == nullptr || msg == nullptr || len == 0u) {
    return Ingest::None;
  }
  const std::uint8_t op = msg[0];
  if (op == kOpClearAll) {
    clear(store);
    return Ingest::Cleared;
  }
  if (op == kOpBody) {
    return Ingest::Body;
  }
  if (op == kOpCallAlert) {
    return Ingest::CallAlert;
  }
  if (op == kOpCallEnd) {
    return Ingest::CallEnd;
  }
  if (op == kOpRemove) {
    if (len < 2u) {
      return Ingest::None;
    }
    const std::uint8_t klen = msg[1];
    if (static_cast<std::size_t>(2u + klen) > len) {
      return Ingest::None;
    }
    const std::uint8_t store_klen = klen > kKeyCap ? kKeyCap : klen;
    const std::int16_t idx = find_key(store, msg + 2, store_klen);
    if (idx < 0) {
      return Ingest::None;
    }
    (void)remove_at(store, static_cast<std::uint8_t>(idx));
    return Ingest::Removed;
  }
  if (op != kOpUpsert) {
    return Ingest::None;
  }
  // UPSERT stub: op, key_len, key, flags, category, monogram, title_len, title,
  //         text_len, text (ignored / usually 0), when_u32_le
  if (len < 2u) {
    return Ingest::None;
  }
  std::size_t i = 1u;
  const std::uint8_t klen = msg[i++];
  if (i + klen + 3u > len) {
    return Ingest::None;
  }
  const std::uint8_t* key = msg + i;
  i += klen;
  const std::uint8_t flags = msg[i++];
  const std::uint8_t category = msg[i++];
  const char mono = static_cast<char>(msg[i++]);
  if (i >= len) {
    return Ingest::None;
  }
  const std::uint8_t tlen = msg[i++];
  if (i + tlen >= len) {
    return Ingest::None;
  }
  const std::uint8_t* title = msg + i;
  i += tlen;
  if (i >= len) {
    return Ingest::None;
  }
  const std::uint8_t xlen = msg[i++];
  if (i + xlen + 4u > len) {
    return Ingest::None;
  }
  i += xlen;  // skip body on stub path
  std::uint32_t when = static_cast<std::uint32_t>(msg[i]) |
                       (static_cast<std::uint32_t>(msg[i + 1]) << 8) |
                       (static_cast<std::uint32_t>(msg[i + 2]) << 16) |
                       (static_cast<std::uint32_t>(msg[i + 3]) << 24);

  // Match against the stored (possibly truncated) key length — comparing the
  // full wire key_len against kKeyCap-clipped entries always missed, so every
  // reconnect UPSERT looked brand-new (duplicate rows + haptic storm).
  const std::uint8_t store_klen = klen > kKeyCap ? kKeyCap : klen;
  std::int16_t idx = find_key(store, key, store_klen);
  const bool is_new = idx < 0;
  if (idx < 0) {
    if (store->count >= kMaxEntries) {
      for (std::uint8_t j = 1u; j < store->count; ++j) {
        store->entries[j - 1u] = store->entries[j];
      }
      --store->count;
    }
    idx = static_cast<std::int16_t>(store->count++);
  }
  Entry& e = store->entries[static_cast<std::uint8_t>(idx)];
  std::memset(&e, 0, sizeof(e));
  copy_str(e.key, kKeyCap, key, klen, &e.key_len);
  // Silent is wire-only (reconnect bulk sync); do not retain it on the row.
  e.flags = static_cast<std::uint8_t>(flags & ~kFlagSilent);
  e.category = category;
  e.monogram = mono;
  copy_str(e.title, kTitleCap, title, tlen, &e.title_len);
  e.when_sec = when;
  e.stale = 0u;
  // Reconnect / shade restore must not wake or DOUBLE — treat as update UX.
  if (is_new && (flags & kFlagSilent) != 0u) {
    return Ingest::StubUpdate;
  }
  return is_new ? Ingest::StubNew : Ingest::StubUpdate;
}

bool parse_body(const std::uint8_t* msg, std::size_t len, char* key_out,
                std::uint8_t key_cap, std::uint8_t* key_len_out, char* text_out,
                std::uint8_t text_cap, std::uint8_t* text_len_out) {
  if (msg == nullptr || len < 2u || msg[0] != kOpBody || key_out == nullptr ||
      text_out == nullptr || key_len_out == nullptr || text_len_out == nullptr) {
    return false;
  }
  std::size_t i = 1u;
  const std::uint8_t klen = msg[i++];
  if (i + klen >= len) {
    return false;
  }
  copy_str(key_out, key_cap, msg + i, klen, key_len_out);
  i += klen;
  if (i >= len) {
    return false;
  }
  const std::uint8_t xlen = msg[i++];
  if (i + xlen > len) {
    return false;
  }
  copy_str(text_out, text_cap, msg + i, xlen, text_len_out);
  return true;
}

bool parse_call_alert(const std::uint8_t* msg, std::size_t len, char* caller_out,
                      std::uint8_t caller_cap, std::uint8_t* caller_len_out) {
  if (msg == nullptr || len < 2u || msg[0] != kOpCallAlert || caller_out == nullptr ||
      caller_len_out == nullptr) {
    return false;
  }
  const std::uint8_t clen = msg[1];
  if (static_cast<std::size_t>(2u + clen) > len) {
    return false;
  }
  copy_str(caller_out, caller_cap, msg + 2, clen, caller_len_out);
  return true;
}

void mark_all_stale(Store* store, bool stale) {
  if (store == nullptr) {
    return;
  }
  for (std::uint8_t i = 0u; i < store->count; ++i) {
    store->entries[i].stale = stale ? 1u : 0u;
  }
}

const Entry* at(const Store* store, std::uint8_t index) {
  if (store == nullptr || index >= store->count) {
    return nullptr;
  }
  return &store->entries[index];
}

bool remove_at(Store* store, std::uint8_t index) {
  if (store == nullptr || index >= store->count) {
    return false;
  }
  for (std::uint8_t j = index + 1u; j < store->count; ++j) {
    store->entries[j - 1u] = store->entries[j];
  }
  --store->count;
  std::memset(&store->entries[store->count], 0, sizeof(Entry));
  return true;
}

bool remove_key(Store* store, const char* key, std::uint8_t key_len) {
  if (store == nullptr || key == nullptr) {
    return false;
  }
  const std::int16_t idx =
      find_key(store, reinterpret_cast<const std::uint8_t*>(key), key_len);
  if (idx < 0) {
    return false;
  }
  return remove_at(store, static_cast<std::uint8_t>(idx));
}

}  // namespace notif
}  // namespace slate
