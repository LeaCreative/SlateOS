#pragma once

#include "asset_pack.hpp"

#include <cstddef>
#include <cstdint>

// 6 KB LRU glyph/icon working set. Pages individual glyphs from a pack reader.

namespace slate {
namespace cache {

constexpr std::size_t kBudgetBytes = 6144u;  // 6 KB (§3.2)

struct Stats {
  std::uint32_t hits = 0u;
  std::uint32_t misses = 0u;
  std::uint32_t evictions = 0u;
  std::uint32_t bytes_used = 0u;

  std::uint32_t accesses() const { return hits + misses; }
  // Hit rate in basis points (0–10000).
  std::uint32_t hit_rate_bp() const {
    const std::uint32_t a = accesses();
    return a == 0u ? 0u : (hits * 10000u) / a;
  }
};

struct Hooks {
  // Read `len` bytes at pack absolute `offset` into `dst`.
  bool (*read_bytes)(std::uint32_t offset, std::uint8_t* dst, std::size_t len,
                     void* ctx) = nullptr;
  void* ctx = nullptr;
};

class GlyphCache {
public:
  void init(const Hooks& hooks, const pack::Index* index);

  // Returns pointer into cache (valid until next mutating call that evicts).
  // On miss, pages from flash via hooks. nullptr if missing/OOM.
  const std::uint8_t* get_glyph(std::uint8_t font_id, std::uint16_t codepoint,
                                std::uint16_t* out_size);

  const std::uint8_t* get_icon(std::uint8_t atlas_id, std::uint16_t icon_id,
                               std::uint16_t* out_size);

  Stats stats() const { return stats_; }
  void reset_stats();

  // Simulate rendering a string; returns hit_rate_bp after the pass.
  std::uint32_t bench_text(std::uint8_t font_id, const char* utf8);

private:
  struct Entry {
    std::uint32_t key = 0u;  // packed font/atlas + id
    std::uint16_t size = 0u;
    std::uint16_t offset = 0u;  // into blob_
    std::uint16_t lru_tick = 0u;
    bool live = false;
  };

  static constexpr std::uint8_t kMaxEntries = 48u;

  std::uint32_t make_glyph_key(std::uint8_t font, std::uint16_t cp) const;
  std::uint32_t make_icon_key(std::uint8_t atlas, std::uint16_t id) const;
  const std::uint8_t* lookup_or_load(std::uint32_t key, std::uint32_t file_off,
                                     std::uint16_t size, std::uint16_t* out_size);
  int find_entry(std::uint32_t key) const;
  int evict_lru();
  bool alloc_bytes(std::uint16_t need, std::uint16_t* out_off);

  Hooks hooks_{};
  const pack::Index* index_ = nullptr;
  Entry entries_[kMaxEntries] = {};
  alignas(4) std::uint8_t blob_[kBudgetBytes] = {};
  std::uint16_t blob_used_ = 0u;
  std::uint16_t lru_clock_ = 1u;
  Stats stats_{};
};

}  // namespace cache
}  // namespace slate
