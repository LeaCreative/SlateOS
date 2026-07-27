#include "glyph_cache.hpp"

#include <cstring>

namespace slate {
namespace cache {

void GlyphCache::init(const Hooks& hooks, const pack::Index* index) {
  hooks_ = hooks;
  index_ = index;
  std::memset(entries_, 0, sizeof(entries_));
  blob_used_ = 0u;
  lru_clock_ = 1u;
  reset_stats();
}

void GlyphCache::reset_stats() { stats_ = Stats{}; }

std::uint32_t GlyphCache::make_glyph_key(std::uint8_t font,
                                         std::uint16_t cp) const {
  return (0u << 24) | (static_cast<std::uint32_t>(font) << 16) | cp;
}

std::uint32_t GlyphCache::make_icon_key(std::uint8_t atlas,
                                        std::uint16_t id) const {
  return (1u << 24) | (static_cast<std::uint32_t>(atlas) << 16) | id;
}

int GlyphCache::find_entry(std::uint32_t key) const {
  for (std::uint8_t i = 0u; i < kMaxEntries; ++i) {
    if (entries_[i].live && entries_[i].key == key) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int GlyphCache::evict_lru() {
  int best = -1;
  std::uint16_t best_tick = 0xFFFFu;
  for (std::uint8_t i = 0u; i < kMaxEntries; ++i) {
    if (!entries_[i].live) {
      continue;
    }
    if (entries_[i].lru_tick <= best_tick) {
      best_tick = entries_[i].lru_tick;
      best = static_cast<int>(i);
    }
  }
  if (best < 0) {
    return -1;
  }
  // Compact blob by marking dead; simple strategy: reset blob if fragmented.
  entries_[best].live = false;
  ++stats_.evictions;
  // Reclaim: if many dead, rebuild (rare).
  std::uint8_t live = 0u;
  for (std::uint8_t i = 0u; i < kMaxEntries; ++i) {
    if (entries_[i].live) {
      ++live;
    }
  }
  if (live == 0u) {
    blob_used_ = 0u;
  }
  return best;
}

bool GlyphCache::alloc_bytes(std::uint16_t need, std::uint16_t* out_off) {
  if (need == 0u || out_off == nullptr) {
    return false;
  }
  while (static_cast<std::uint32_t>(blob_used_) + need > kBudgetBytes) {
    if (evict_lru() < 0) {
      // Force clear.
      std::memset(entries_, 0, sizeof(entries_));
      blob_used_ = 0u;
      break;
    }
    // After eviction without compact, may still not fit — compact all live.
    if (static_cast<std::uint32_t>(blob_used_) + need > kBudgetBytes) {
      alignas(4) static std::uint8_t tmp[kBudgetBytes];
      std::uint16_t n = 0u;
      for (std::uint8_t i = 0u; i < kMaxEntries; ++i) {
        if (!entries_[i].live) {
          continue;
        }
        std::memcpy(tmp + n, blob_ + entries_[i].offset, entries_[i].size);
        entries_[i].offset = n;
        n = static_cast<std::uint16_t>(n + entries_[i].size);
      }
      std::memcpy(blob_, tmp, n);
      blob_used_ = n;
    }
  }
  if (static_cast<std::uint32_t>(blob_used_) + need > kBudgetBytes) {
    return false;
  }
  *out_off = blob_used_;
  blob_used_ = static_cast<std::uint16_t>(blob_used_ + need);
  stats_.bytes_used = blob_used_;
  return true;
}

const std::uint8_t* GlyphCache::lookup_or_load(std::uint32_t key,
                                               std::uint32_t file_off,
                                               std::uint16_t size,
                                               std::uint16_t* out_size) {
  const int hit = find_entry(key);
  if (hit >= 0) {
    ++stats_.hits;
    entries_[hit].lru_tick = ++lru_clock_;
    if (out_size) {
      *out_size = entries_[hit].size;
    }
    return blob_ + entries_[hit].offset;
  }
  ++stats_.misses;
  if (hooks_.read_bytes == nullptr || size == 0u || size > kBudgetBytes) {
    return nullptr;
  }

  int slot = -1;
  for (std::uint8_t i = 0u; i < kMaxEntries; ++i) {
    if (!entries_[i].live) {
      slot = static_cast<int>(i);
      break;
    }
  }
  if (slot < 0) {
    slot = evict_lru();
    if (slot < 0) {
      return nullptr;
    }
  }

  std::uint16_t off = 0u;
  if (!alloc_bytes(size, &off)) {
    return nullptr;
  }
  if (!hooks_.read_bytes(file_off, blob_ + off, size, hooks_.ctx)) {
    return nullptr;
  }
  entries_[slot].key = key;
  entries_[slot].size = size;
  entries_[slot].offset = off;
  entries_[slot].lru_tick = ++lru_clock_;
  entries_[slot].live = true;
  if (out_size) {
    *out_size = size;
  }
  return blob_ + off;
}

const std::uint8_t* GlyphCache::get_glyph(std::uint8_t font_id,
                                          std::uint16_t codepoint,
                                          std::uint16_t* out_size) {
  if (index_ == nullptr) {
    return nullptr;
  }
  const pack::FontInfo* f = pack::find_font(*index_, font_id);
  if (f == nullptr) {
    return nullptr;
  }
  const pack::GlyphDir* g = pack::find_glyph(*f, codepoint);
  if (g == nullptr) {
    return nullptr;
  }
  return lookup_or_load(make_glyph_key(font_id, codepoint), g->offset, g->size,
                        out_size);
}

const std::uint8_t* GlyphCache::get_icon(std::uint8_t atlas_id,
                                         std::uint16_t icon_id,
                                         std::uint16_t* out_size) {
  if (index_ == nullptr) {
    return nullptr;
  }
  const pack::AtlasInfo* a = pack::find_atlas(*index_, atlas_id);
  if (a == nullptr) {
    return nullptr;
  }
  const pack::IconDir* ic = pack::find_icon(*a, icon_id);
  if (ic == nullptr) {
    return nullptr;
  }
  return lookup_or_load(make_icon_key(atlas_id, icon_id), ic->offset, ic->size,
                        out_size);
}

std::uint32_t GlyphCache::bench_text(std::uint8_t font_id, const char* utf8) {
  if (utf8 == nullptr) {
    return 0u;
  }
  reset_stats();
  // Warm then measure: two passes — first fills cache, second reports hits.
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1) {
      reset_stats();
    }
    for (const char* p = utf8; *p != '\0'; ++p) {
      std::uint16_t sz = 0u;
      (void)get_glyph(font_id, static_cast<std::uint8_t>(*p), &sz);
    }
  }
  return stats_.hit_rate_bp();
}

}  // namespace cache
}  // namespace slate
