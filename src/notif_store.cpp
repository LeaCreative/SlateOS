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

std::int16_t find_key(const Store* s, const std::uint8_t* key, std::uint8_t key_len) {
  for (std::uint8_t i = 0u; i < s->count; ++i) {
    if (key_eq(s->entries[i], key, key_len)) {
      return static_cast<std::int16_t>(i);
    }
  }
  return -1;
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

void init(Store* store) {
  if (store == nullptr) {
    return;
  }
  std::memset(store, 0, sizeof(*store));
}

void clear(Store* store) {
  init(store);
}

bool on_system_message(Store* store, const std::uint8_t* msg, std::size_t len) {
  if (store == nullptr || msg == nullptr || len == 0u) {
    return false;
  }
  const std::uint8_t op = msg[0];
  if (op == kOpClearAll) {
    clear(store);
    return true;
  }
  if (op == kOpRemove) {
    if (len < 2u) {
      return false;
    }
    const std::uint8_t klen = msg[1];
    if (static_cast<std::size_t>(2u + klen) > len) {
      return false;
    }
    const std::int16_t idx = find_key(store, msg + 2, klen);
    if (idx < 0) {
      return false;
    }
    return remove_at(store, static_cast<std::uint8_t>(idx));
  }
  if (op != kOpUpsert) {
    return false;
  }
  // UPSERT: op, key_len, key, flags, category, monogram, title_len, title,
  //         text_len, text, when_u32_le
  if (len < 2u) {
    return false;
  }
  std::size_t i = 1u;
  const std::uint8_t klen = msg[i++];
  if (i + klen + 3u > len) {
    return false;
  }
  const std::uint8_t* key = msg + i;
  i += klen;
  const std::uint8_t flags = msg[i++];
  const std::uint8_t category = msg[i++];
  const char mono = static_cast<char>(msg[i++]);
  if (i >= len) {
    return false;
  }
  const std::uint8_t tlen = msg[i++];
  if (i + tlen >= len) {
    return false;
  }
  const std::uint8_t* title = msg + i;
  i += tlen;
  if (i >= len) {
    return false;
  }
  const std::uint8_t xlen = msg[i++];
  if (i + xlen + 4u > len) {
    return false;
  }
  const std::uint8_t* text = msg + i;
  i += xlen;
  std::uint32_t when = static_cast<std::uint32_t>(msg[i]) |
                       (static_cast<std::uint32_t>(msg[i + 1]) << 8) |
                       (static_cast<std::uint32_t>(msg[i + 2]) << 16) |
                       (static_cast<std::uint32_t>(msg[i + 3]) << 24);

  std::int16_t idx = find_key(store, key, klen);
  if (idx < 0) {
    if (store->count >= kMaxEntries) {
      // Drop oldest.
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
  e.flags = flags;
  e.category = category;
  e.monogram = mono;
  copy_str(e.title, kTitleCap, title, tlen, &e.title_len);
  copy_str(e.text, kTextCap, text, xlen, &e.text_len);
  e.when_sec = when;
  e.stale = 0u;
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

}  // namespace notif
}  // namespace slate
