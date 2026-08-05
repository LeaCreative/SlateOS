#include "renderer.hpp"

#include <cstdio>
#include <cstdint>

// Display stubs: the culling tests never flush, but renderer.cpp links these.
namespace st7789 {
void set_window(std::uint16_t, std::uint16_t, std::uint16_t, std::uint16_t) {}
bool write_pixels(const std::uint8_t*, std::uint32_t) { return true; }
}  // namespace st7789

// N-18: draw ops now skip rows outside the active tile instead of letting
// put_pixel() reject them one pixel at a time. Culling must be a pure fast
// path — identical output, less work — so these tests check the boundaries
// where an off-by-one would corrupt the display on sealed hardware.

namespace {

int g_failures = 0;

void expect(const char* name, bool cond) {
  if (!cond) {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

std::uint16_t pixel_at(const Renderer& r, std::uint32_t x, std::uint32_t row_in_tile) {
  const std::uint8_t* b = r.tile_buffer();
  const std::uint32_t off = (row_in_tile * kDisplayWidth + x) * 2u;
  return static_cast<std::uint16_t>((b[off] << 8) | b[off + 1]);
}

bool tile_is_uniform(const Renderer& r, std::uint16_t want) {
  for (std::uint32_t row = 0u; row < kTileHeight; ++row) {
    for (std::uint32_t x = 0u; x < kDisplayWidth; ++x) {
      if (pixel_at(r, x, row) != want) return false;
    }
  }
  return true;
}

// A full-screen fill must paint every tile completely, exactly as it did when
// put_pixel did the rejecting.
void test_fullscreen_fill_reaches_every_tile() {
  bool all_ok = true;
  for (std::uint32_t t = 0u; t < kTileCount; ++t) {
    Renderer r;
    r.set_tile_filter(static_cast<std::int32_t>(t));
    r.clear_clip();
    r.clear_tile_buffer(0x0000u);
    r.fill_rect(0u, 0u, kDisplayWidth, kDisplayHeight, 0xF800u);
    if (!tile_is_uniform(r, 0xF800u)) {
      std::printf("  tile %u not fully painted\n", t);
      all_ok = false;
    }
  }
  expect("full-screen fill paints all 30 tiles", all_ok);
}

// A rect confined to one tile must paint that tile and leave the others clean.
void test_rect_confined_to_one_tile() {
  constexpr std::uint32_t kTarget = 7u;
  const std::uint16_t y = static_cast<std::uint16_t>(kTarget * kTileHeight);
  bool ok = true;
  for (std::uint32_t t = 0u; t < kTileCount; ++t) {
    Renderer r;
    r.set_tile_filter(static_cast<std::int32_t>(t));
    r.clear_clip();
    r.clear_tile_buffer(0x0000u);
    r.fill_rect(0u, y, kDisplayWidth, kTileHeight, 0x07E0u);
    const bool painted = tile_is_uniform(r, 0x07E0u);
    const bool blank = tile_is_uniform(r, 0x0000u);
    if (t == kTarget ? !painted : !blank) {
      std::printf("  tile %u wrong (painted=%d blank=%d)\n", t, painted, blank);
      ok = false;
    }
  }
  expect("single-tile rect lands only in its tile", ok);
}

// The hard case: a rect straddling a tile boundary must paint the exact rows
// on both sides. One row off here is a visible seam.
void test_rect_straddling_boundary() {
  // Rows 62..65 straddle the tile 7/8 boundary at row 64.
  const std::uint16_t y = 62u;
  const std::uint16_t h = 4u;
  Renderer up;
  up.set_tile_filter(7);
  up.clear_clip();
  up.clear_tile_buffer(0x0000u);
  up.fill_rect(0u, y, kDisplayWidth, h, 0x001Fu);

  bool ok = true;
  // Tile 7 covers rows 56..63 → only its last two rows (62, 63) are painted.
  for (std::uint32_t row = 0u; row < kTileHeight; ++row) {
    const std::uint16_t got = pixel_at(up, 0u, row);
    const std::uint16_t want = (row >= 6u) ? 0x001Fu : 0x0000u;
    if (got != want) {
      std::printf("  upper tile row %u got %04x want %04x\n", row, got, want);
      ok = false;
    }
  }

  Renderer lo;
  lo.set_tile_filter(8);
  lo.clear_clip();
  lo.clear_tile_buffer(0x0000u);
  lo.fill_rect(0u, y, kDisplayWidth, h, 0x001Fu);
  // Tile 8 covers rows 64..71 → only its first two rows (64, 65) are painted.
  for (std::uint32_t row = 0u; row < kTileHeight; ++row) {
    const std::uint16_t got = pixel_at(lo, 0u, row);
    const std::uint16_t want = (row < 2u) ? 0x001Fu : 0x0000u;
    if (got != want) {
      std::printf("  lower tile row %u got %04x want %04x\n", row, got, want);
      ok = false;
    }
  }
  expect("rect straddling a tile boundary is exact", ok);
}

// Blits carry a source offset, so culling must shift the source row index by
// the same amount it skips — otherwise glyphs render with the wrong slice.
void test_blit_source_rows_align() {
  // 8x16 bitmap, every row a distinct value, placed straddling rows 60..75.
  constexpr std::uint16_t kW = 8u;
  constexpr std::uint16_t kH = 16u;
  std::uint16_t src[kW * kH];
  for (std::uint16_t row = 0u; row < kH; ++row) {
    for (std::uint16_t col = 0u; col < kW; ++col) {
      src[row * kW + col] = static_cast<std::uint16_t>(0x100u + row);
    }
  }
  const std::uint16_t y = 60u;

  bool ok = true;
  for (std::uint32_t t = 7u; t <= 9u; ++t) {
    Renderer r;
    r.set_tile_filter(static_cast<std::int32_t>(t));
    r.clear_clip();
    r.clear_tile_buffer(0x0000u);
    r.blit_rgb565(0u, y, kW, kH, src);
    const std::uint32_t tile_top = t * kTileHeight;
    for (std::uint32_t row = 0u; row < kTileHeight; ++row) {
      const std::uint32_t screen_y = tile_top + row;
      const bool inside = screen_y >= y && screen_y < (y + kH);
      const std::uint16_t want =
          inside ? static_cast<std::uint16_t>(0x100u + (screen_y - y)) : 0x0000u;
      const std::uint16_t got = pixel_at(r, 0u, row);
      if (got != want) {
        std::printf("  tile %u row %u got %04x want %04x\n", t, row, got, want);
        ok = false;
      }
    }
  }
  expect("blit source rows stay aligned across tiles", ok);
}

// Unfiltered rendering (tile_filter < 0) must be unaffected by the culling.
void test_unfiltered_still_draws() {
  Renderer r;
  r.set_tile_filter(Renderer::kNoTileFilter);
  r.clear_clip();
  r.clear_tile_buffer(0x0000u);
  r.fill_rect(0u, 0u, kDisplayWidth, 4u, 0xFFFFu);
  expect("unfiltered draw still writes row 0", pixel_at(r, 0u, 0u) == 0xFFFFu);
}

}  // namespace

int main() {
  test_fullscreen_fill_reaches_every_tile();
  test_rect_confined_to_one_tile();
  test_rect_straddling_boundary();
  test_blit_source_rows_align();
  test_unfiltered_still_draws();
  if (g_failures != 0) {
    std::printf("%d FAILURES\n", g_failures);
    return 1;
  }
  std::printf("all renderer culling tests passed\n");
  return 0;
}
