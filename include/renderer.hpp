#pragma once

#include <cstddef>
#include <cstdint>

// ── Tile geometry ─────────────────────────────────────────────────────────────
//
// RAM cost for tile buffers = 2 × (kDisplayWidth × kTileHeight × 2) bytes.
//
//   kTileHeight │ buffer pair RAM │ notes
//   ────────────┼─────────────────┼──────────────────────────────────────────
//       4       │  3,840 B        │ minimum for clean double-buffer handoff
//       8       │  7,680 B        │ ← project default (§3.2 budget)
//      12       │ 11,520 B        │ fewer DMA kicks per full redraw
//      16       │ 15,360 B        │ was the erroneous v0.1 target; over budget
//
// Change kTileHeight here; everything else is computed from it.

static constexpr std::uint32_t kDisplayWidth  = 240u;
static constexpr std::uint32_t kDisplayHeight = 240u;
static constexpr std::uint32_t kTileHeight    = 8u;
static constexpr std::uint32_t kTileCount     = kDisplayHeight / kTileHeight;
// Bytes per tile buffer (single buffer).
static constexpr std::uint32_t kTileBytes     = kDisplayWidth * kTileHeight * 2u;

// ── Colour helpers ────────────────────────────────────────────────────────────

// Pack r(0-31), g(0-63), b(0-31) into RGB565 big-endian word.
constexpr std::uint16_t rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r & 0x1Fu) << 11u) |
                                      ((g & 0x3Fu) << 5u)  |
                                       (b & 0x1Fu));
}

// Pack 8-bit r/g/b (0-255) into RGB565.
constexpr std::uint16_t rgb888_to_565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return rgb565(static_cast<std::uint8_t>(r >> 3),
                  static_cast<std::uint8_t>(g >> 2),
                  static_cast<std::uint8_t>(b >> 3));
}

// Convert RGB332 byte to RGB565.
constexpr std::uint16_t rgb332_to_565(std::uint8_t c) {
    const std::uint8_t r3 = (c >> 5u) & 0x07u;
    const std::uint8_t g3 = (c >> 2u) & 0x07u;
    const std::uint8_t b2 = c & 0x03u;
    // Scale: r 3→5 bits, g 3→6 bits, b 2→5 bits.
    return rgb565(static_cast<std::uint8_t>(r3 << 2u | r3 >> 1u),
                  static_cast<std::uint8_t>(g3 << 3u | g3),
                  static_cast<std::uint8_t>(b2 << 3u | b2 << 1u | b2 >> 1u));
}

// ── Dirty-rectangle tracker ───────────────────────────────────────────────────
//
// The display is divided into kTileCount horizontal tile rows.  Each tile is
// represented by one bit in a 32-bit word.  kTileCount=30 fits comfortably.
// Mark a pixel region dirty → only those tiles will be transmitted on flush().

class DirtyMap {
public:
    void clear() { mask_ = 0u; }
    void mark_all() { mask_ = (1u << kTileCount) - 1u; }
    void mark_tile(std::uint32_t tile_row) { mask_ |= (1u << tile_row); }

    // Mark every tile row that overlaps the pixel rectangle [y0, y1].
    void mark_rect(std::uint32_t y0, std::uint32_t y1) {
        const std::uint32_t t0 = y0 / kTileHeight;
        const std::uint32_t t1 = y1 / kTileHeight;
        for (std::uint32_t t = t0; t <= t1 && t < kTileCount; ++t) {
            mark_tile(t);
        }
    }

    bool is_dirty(std::uint32_t tile_row) const {
        return (mask_ & (1u << tile_row)) != 0u;
    }

    bool any_dirty() const { return mask_ != 0u; }

    std::uint32_t mask() const { return mask_; }

private:
    std::uint32_t mask_ = 0u;
};

// ── Renderer ──────────────────────────────────────────────────────────────────

class Renderer {
public:
    Renderer();

    // Mark the entire display dirty and fill every pixel with `colour`.
    void fill_screen(std::uint16_t colour);

    // Draw operations — automatically dirty the affected tiles.
    void fill_rect(std::uint16_t x, std::uint16_t y,
                   std::uint16_t w, std::uint16_t h,
                   std::uint16_t colour);

    void draw_line(std::int16_t x0, std::int16_t y0,
                   std::int16_t x1, std::int16_t y1,
                   std::uint16_t colour);

    void draw_circle(std::int16_t cx, std::int16_t cy, std::int16_t radius,
                     std::uint16_t colour);

    void draw_round_rect(std::uint16_t x, std::uint16_t y,
                         std::uint16_t w, std::uint16_t h,
                         std::uint16_t radius,
                         std::uint16_t colour);

    // `a0` and `a1` are angles in degrees [0-360).
    void draw_arc(std::int16_t cx, std::int16_t cy, std::int16_t radius,
                  std::int16_t a0, std::int16_t a1,
                  std::uint16_t colour);

    // Blit an RGB565 source bitmap into (x, y).
    void blit_rgb565(std::uint16_t x, std::uint16_t y,
                     std::uint16_t w, std::uint16_t h,
                     const std::uint16_t* src);

    // Blit an RGB332 source bitmap (byte-per-pixel) into (x, y).
    void blit_rgb332(std::uint16_t x, std::uint16_t y,
                     std::uint16_t w, std::uint16_t h,
                     const std::uint8_t* src);

    // Blit a 1-bit source bitmap (row-major, MSB first) into (x, y),
    // drawing `fg` for set bits and `bg` for clear bits.
    void blit_1bit(std::uint16_t x, std::uint16_t y,
                   std::uint16_t w, std::uint16_t h,
                   const std::uint8_t* src,
                   std::uint16_t fg, std::uint16_t bg);

    // Flush all dirty tiles to the display and clear the dirty map.
    // Returns the number of tiles actually transmitted.
    std::uint32_t flush();

    const DirtyMap& dirty_map() const { return dirty_; }

private:
    // Draw a single pixel — bounds-checked, marks tile dirty.
    void put_pixel(std::uint16_t x, std::uint16_t y, std::uint16_t colour);

    // Two tile-sized scratch buffers.
    //   buf_[0]: active render target — put_pixel() writes here.
    //   buf_[1]: staging copy used for transmit snapshots (future async DMA).
    // NOTE (M1 model): buf_[0] holds only one tile row at a time.  flush()
    // transmits buf_[0] for every dirty tile, so this is correct only when
    // all tiles contain identical data (fill_screen) or when the caller re-runs
    // draw ops between tile flushes.  M3 replaces this with per-tile re-render
    // from the retained display list.
    alignas(4) std::uint8_t buf_[2][kTileBytes];

    DirtyMap dirty_;
};
