#include "asset_pack.hpp"
#include "asset_xfer.hpp"
#include "glyph_cache.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_fails = 0;
void expect(const char* name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    ++g_fails;
  } else {
    std::printf("ok   %s\n", name);
  }
}

std::vector<std::uint8_t> g_pack;
std::vector<std::uint8_t> g_staging;
std::vector<std::uint8_t> g_tx;

bool read_bytes(std::uint32_t offset, std::uint8_t* dst, std::size_t len, void*) {
  if (static_cast<std::size_t>(offset) + len > g_pack.size()) {
    return false;
  }
  std::memcpy(dst, g_pack.data() + offset, len);
  return true;
}

bool write_staging(std::uint32_t offset, const std::uint8_t* data, std::size_t len,
                   void*) {
  if (g_staging.size() < offset + len) {
    g_staging.resize(offset + len);
  }
  std::memcpy(g_staging.data() + offset, data, len);
  return true;
}

bool commit_staging(const char* name, std::uint32_t total, std::uint32_t hash,
                    void*) {
  (void)name;
  if (g_staging.size() != total) {
    return false;
  }
  // Hash field in pack header is FNV of bytes from offset 12.
  if (total < 20u) {
    return false;
  }
  const std::uint32_t got =
      slate::pack::fnv1a(g_staging.data() + 12, total - 12u);
  if (got != hash) {
    return false;
  }
  g_pack = g_staging;
  return true;
}

bool abort_staging(void*) {
  g_staging.clear();
  return true;
}

bool send_fn(const std::uint8_t* msg, std::size_t len, void*) {
  g_tx.insert(g_tx.end(), msg, msg + len);
  return true;
}

}  // namespace

static void test_parse_and_cache() {
  // Load pack built by tools/assetpack/pack.py (run in CMake or pre-step).
  FILE* f = std::fopen("shared/generated/core.slap", "rb");
  if (f == nullptr) {
    // Try from repo root relative to build dir.
    f = std::fopen("../../shared/generated/core.slap", "rb");
  }
  if (f == nullptr) {
    f = std::fopen("../shared/generated/core.slap", "rb");
  }
  expect("open core.slap", f != nullptr);
  if (f == nullptr) {
    return;
  }
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  g_pack.resize(static_cast<std::size_t>(sz));
  expect("read pack",
         std::fread(g_pack.data(), 1, g_pack.size(), f) == g_pack.size());
  std::fclose(f);

  slate::pack::Index idx{};
  const auto st =
      slate::pack::parse(g_pack.data(), g_pack.size(), &idx);
  expect("parse ok", st == slate::pack::Status::Ok);
  expect("has font0", slate::pack::find_font(idx, 0) != nullptr);

  slate::cache::GlyphCache cache;
  slate::cache::Hooks hk;
  hk.read_bytes = &read_bytes;
  cache.init(hk, &idx);

  // Text-heavy: digits repeated — second pass should be ~100% hits.
  const char* heavy =
      "01234567890123456789012345678901234567890123456789"
      "01234567890123456789012345678901234567890123456789";
  const std::uint32_t bp = cache.bench_text(0, heavy);
  std::printf("  glyph cache hit rate = %u.%02u%%\n", bp / 100u, bp % 100u);
  expect("hit rate >= 95%", bp >= 9500u);
  expect("cache within 6KB", cache.stats().bytes_used <= slate::cache::kBudgetBytes);
}

static void test_skip_unknown_record() {
  // Minimal hand-built pack with an unknown TLV that must be skipped.
  std::vector<std::uint8_t> payload;
  // empty payload
  std::vector<std::uint8_t> index;
  // unknown type 0x55, id 0, length 3, body aaa
  index.push_back(0x55);
  index.push_back(0);
  index.push_back(3);
  index.push_back(0);
  index.push_back('a');
  index.push_back('a');
  index.push_back('a');
  // END
  index.push_back(0xF0);
  index.push_back(0);
  index.push_back(0);
  index.push_back(0);

  const std::uint32_t index_off = 20;
  const std::uint32_t index_len = static_cast<std::uint32_t>(index.size());
  std::vector<std::uint8_t> rest;
  rest.push_back(static_cast<std::uint8_t>(index_off));
  rest.push_back(static_cast<std::uint8_t>(index_off >> 8));
  rest.push_back(static_cast<std::uint8_t>(index_off >> 16));
  rest.push_back(static_cast<std::uint8_t>(index_off >> 24));
  rest.push_back(static_cast<std::uint8_t>(index_len));
  rest.push_back(static_cast<std::uint8_t>(index_len >> 8));
  rest.push_back(static_cast<std::uint8_t>(index_len >> 16));
  rest.push_back(static_cast<std::uint8_t>(index_len >> 24));
  rest.insert(rest.end(), index.begin(), index.end());
  const std::uint32_t hash = slate::pack::fnv1a(rest.data(), rest.size());

  std::vector<std::uint8_t> pack;
  pack.insert(pack.end(), {'S', 'L', 'A', 'P'});
  pack.push_back(1);
  pack.push_back(0);  // version
  pack.push_back(0);
  pack.push_back(0);  // flags
  pack.push_back(static_cast<std::uint8_t>(hash));
  pack.push_back(static_cast<std::uint8_t>(hash >> 8));
  pack.push_back(static_cast<std::uint8_t>(hash >> 16));
  pack.push_back(static_cast<std::uint8_t>(hash >> 24));
  pack.insert(pack.end(), rest.begin(), rest.end());

  slate::pack::Index idx{};
  expect("skip unknown",
         slate::pack::parse(pack.data(), pack.size(), &idx) ==
             slate::pack::Status::Ok);
}

static void test_asset_xfer() {
  g_pack = {'S', 'L', 'A', 'P'};  // will be replaced on commit
  // Build a tiny valid pack for transfer.
  test_skip_unknown_record();  // leaves nothing in g_pack

  // Use core.slap if available.
  FILE* f = std::fopen("../../shared/generated/core.slap", "rb");
  if (f == nullptr) {
    f = std::fopen("shared/generated/core.slap", "rb");
  }
  if (f == nullptr) {
    expect("xfer pack present", false);
    return;
  }
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<std::uint8_t> file(static_cast<std::size_t>(sz));
  (void)std::fread(file.data(), 1, file.size(), f);
  std::fclose(f);

  const std::uint32_t hash =
      slate::pack::fnv1a(file.data() + 12, file.size() - 12);

  slate::asset::Receiver rx;
  slate::asset::Hooks hk;
  hk.write_staging = &write_staging;
  hk.commit_staging = &commit_staging;
  hk.abort_staging = &abort_staging;
  hk.send = &send_fn;
  g_staging.clear();
  g_tx.clear();
  rx.init(hk);

  // BEGIN
  std::vector<std::uint8_t> begin = {slate::asset::kOpBegin};
  begin.push_back(1);
  begin.push_back(0);  // id
  const auto total = static_cast<std::uint32_t>(file.size());
  for (int i = 0; i < 4; ++i) {
    begin.push_back(static_cast<std::uint8_t>((total >> (8 * i)) & 0xFF));
  }
  for (int i = 0; i < 4; ++i) {
    begin.push_back(static_cast<std::uint8_t>((hash >> (8 * i)) & 0xFF));
  }
  const char* name = "core.slap";
  begin.push_back(static_cast<std::uint8_t>(std::strlen(name)));
  begin.insert(begin.end(), name, name + std::strlen(name));
  rx.on_message(begin.data(), begin.size());
  expect("xfer active", rx.transfer_active());

  // Chunks of 200 bytes
  std::uint32_t off = 0;
  while (off < total) {
    const std::uint32_t n = (total - off > 200u) ? 200u : (total - off);
    std::vector<std::uint8_t> chunk = {slate::asset::kOpChunk, 1, 0};
    for (int i = 0; i < 4; ++i) {
      chunk.push_back(static_cast<std::uint8_t>((off >> (8 * i)) & 0xFF));
    }
    chunk.insert(chunk.end(), file.begin() + off, file.begin() + off + n);
    rx.set_yield_busy(false);
    rx.on_message(chunk.data(), chunk.size());
    off += n;
  }
  expect("received all", rx.received() == total);

  std::uint8_t commit[3] = {slate::asset::kOpCommit, 1, 0};
  rx.on_message(commit, 3);
  expect("commit done", !rx.transfer_active());
  expect("pack replaced", g_pack.size() == file.size());
}

int main() {
  test_parse_and_cache();
  test_skip_unknown_record();
  test_asset_xfer();
  if (g_fails) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all asset tests passed\n");
  return 0;
}
