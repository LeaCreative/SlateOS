#include "renderer.hpp"
#include "st7789.hpp"

#include <algorithm>
#include <cstring>

// Integer sine/cosine approximation (fixed-point, scaled to 256).
// Avoids pulling in <cmath> and libm for a Cortex-M4 bare-metal build.
// Range: angle 0-359 degrees.  Returns value in [-256, 256].
namespace {

// 91-entry first-quadrant sine table, scaled ×256, built at compile time.
// sin_table[i] = round(sin(i * π/180) * 256),  i = 0..90
static const std::int16_t kSinTable[91] = {
      0,   4,   9,  13,  18,  22,  27,  31,  36,  40,  44,  49,  53,  57,  62,
     66,  70,  74,  79,  83,  87,  91,  95,  99, 103, 107, 111, 115, 119, 122,
    126, 130, 133, 137, 141, 144, 147, 151, 154, 158, 161, 164, 167, 171, 174,
    177, 180, 182, 185, 188, 191, 193, 196, 198, 201, 203, 205, 208, 210, 212,
    214, 216, 218, 219, 221, 223, 224, 226, 227, 228, 230, 231, 232, 233, 234,
    235, 236, 237, 237, 238, 239, 239, 240, 240, 241, 241, 241, 242, 242, 242,
    242
};

std::int32_t isin(std::int32_t deg) {
    deg = ((deg % 360) + 360) % 360;
    if (deg <= 90)  return  kSinTable[deg];
    if (deg <= 180) return  kSinTable[180 - deg];
    if (deg <= 270) return -kSinTable[deg - 180];
    return                  -kSinTable[360 - deg];
}

std::int32_t icos(std::int32_t deg) {
    return isin(deg + 90);
}

// Write one RGB565 pixel (big-endian) into a tile buffer at pixel offset.
inline void tile_put(std::uint8_t* tile, std::uint32_t offset, std::uint16_t colour) {
    tile[offset * 2u]     = static_cast<std::uint8_t>(colour >> 8u);
    tile[offset * 2u + 1u] = static_cast<std::uint8_t>(colour);
}

}  // namespace

// ── Renderer implementation ───────────────────────────────────────────────────

Renderer::Renderer() {
    std::memset(buf_, 0, sizeof(buf_));
    dirty_.clear();
}

void Renderer::put_pixel(std::uint16_t x, std::uint16_t y, std::uint16_t colour) {
    if (x >= kDisplayWidth || y >= kDisplayHeight) return;
    if (!in_clip(x, y)) return;

    const std::uint32_t tile_row = y / kTileHeight;
    if (tile_filter_ >= 0 &&
        static_cast<std::uint32_t>(tile_filter_) != tile_row) {
        return;
    }

    const std::uint32_t row_in_tile = y % kTileHeight;
    const std::uint32_t offset = row_in_tile * kDisplayWidth + x;
    tile_put(buf_[0], offset, colour);
    dirty_.mark_tile(tile_row);
}

void Renderer::fill_screen(std::uint16_t colour) {
    // Pre-fill buf_[0] with the solid colour.  Every tile row will contain this
    // same data, so flush() can transmit buf_[0] for each dirty tile without
    // needing to re-render between transmits — they are all identical.
    const std::uint8_t hi = static_cast<std::uint8_t>(colour >> 8u);
    const std::uint8_t lo = static_cast<std::uint8_t>(colour);
    for (std::uint32_t i = 0u; i < kTileBytes / 2u; ++i) {
        buf_[0][i * 2u]      = hi;
        buf_[0][i * 2u + 1u] = lo;
    }
    dirty_.mark_all();
}

void Renderer::fill_rect(std::uint16_t x, std::uint16_t y,
                          std::uint16_t w, std::uint16_t h,
                          std::uint16_t colour) {
    const std::uint16_t x1 = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(x + w, kDisplayWidth) - 1u);
    const std::uint16_t y1 = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(y + h, kDisplayHeight) - 1u);

    if (x >= kDisplayWidth || y >= kDisplayHeight) return;
    dirty_.mark_rect(y, y1);

    for (std::uint16_t py = y; py <= y1; ++py) {
        for (std::uint16_t px = x; px <= x1; ++px) {
            put_pixel(px, py, colour);
        }
    }
}

void Renderer::draw_line(std::int16_t x0, std::int16_t y0,
                          std::int16_t x1, std::int16_t y1,
                          std::uint16_t colour) {
    // Bresenham's line algorithm.
    const std::int16_t adx = static_cast<std::int16_t>(x1 - x0);
    const std::int16_t ady = static_cast<std::int16_t>(y1 - y0);
    std::int16_t dx = (adx < 0) ? static_cast<std::int16_t>(-adx) : adx;
    std::int16_t dy = (ady < 0) ? ady : static_cast<std::int16_t>(-ady);
    std::int16_t sx = (x0 < x1) ? 1 : -1;
    std::int16_t sy = (y0 < y1) ? 1 : -1;
    std::int16_t err = static_cast<std::int16_t>(dx + dy);

    while (true) {
        if (x0 >= 0 && x0 < static_cast<std::int16_t>(kDisplayWidth) &&
            y0 >= 0 && y0 < static_cast<std::int16_t>(kDisplayHeight)) {
            put_pixel(static_cast<std::uint16_t>(x0),
                      static_cast<std::uint16_t>(y0), colour);
        }
        if (x0 == x1 && y0 == y1) break;
        std::int16_t e2 = static_cast<std::int16_t>(2 * err);
        if (e2 >= dy) { err = static_cast<std::int16_t>(err + dy); x0 = static_cast<std::int16_t>(x0 + sx); }
        if (e2 <= dx) { err = static_cast<std::int16_t>(err + dx); y0 = static_cast<std::int16_t>(y0 + sy); }
    }
}

void Renderer::draw_circle(std::int16_t cx, std::int16_t cy, std::int16_t radius,
                             std::uint16_t colour) {
    // Midpoint circle algorithm.
    std::int16_t x = radius;
    std::int16_t y = 0;
    std::int16_t err = 0;

    auto plot8 = [&](std::int16_t dx, std::int16_t dy) {
        auto px = [&](std::int16_t px, std::int16_t py) {
            if (px >= 0 && px < static_cast<std::int16_t>(kDisplayWidth) &&
                py >= 0 && py < static_cast<std::int16_t>(kDisplayHeight)) {
                put_pixel(static_cast<std::uint16_t>(px),
                          static_cast<std::uint16_t>(py), colour);
            }
        };
        px(static_cast<std::int16_t>(cx + dx), static_cast<std::int16_t>(cy + dy));
        px(static_cast<std::int16_t>(cx - dx), static_cast<std::int16_t>(cy + dy));
        px(static_cast<std::int16_t>(cx + dx), static_cast<std::int16_t>(cy - dy));
        px(static_cast<std::int16_t>(cx - dx), static_cast<std::int16_t>(cy - dy));
        px(static_cast<std::int16_t>(cx + dy), static_cast<std::int16_t>(cy + dx));
        px(static_cast<std::int16_t>(cx - dy), static_cast<std::int16_t>(cy + dx));
        px(static_cast<std::int16_t>(cx + dy), static_cast<std::int16_t>(cy - dx));
        px(static_cast<std::int16_t>(cx - dy), static_cast<std::int16_t>(cy - dx));
    };

    while (x >= y) {
        plot8(x, y);
        ++y;
        if (err <= 0) {
            err = static_cast<std::int16_t>(err + 2 * y + 1);
        } else {
            --x;
            err = static_cast<std::int16_t>(err + 2 * (y - x) + 1);
        }
    }
}

void Renderer::draw_round_rect(std::uint16_t x, std::uint16_t y,
                                 std::uint16_t w, std::uint16_t h,
                                 std::uint16_t radius,
                                 std::uint16_t colour) {
    if (radius == 0u) {
        // Degenerate — draw as regular rect outline.
        fill_rect(x, y, w, 1, colour);
        fill_rect(x, static_cast<std::uint16_t>(y + h - 1u), w, 1, colour);
        fill_rect(x, y, 1, h, colour);
        fill_rect(static_cast<std::uint16_t>(x + w - 1u), y, 1, h, colour);
        return;
    }

    const std::uint16_t r = std::min<std::uint16_t>(
        radius, static_cast<std::uint16_t>(std::min(w, h) / 2u));

    // Straight edges.
    fill_rect(static_cast<std::uint16_t>(x + r), y,
              static_cast<std::uint16_t>(w - 2u * r), 1u, colour);
    fill_rect(static_cast<std::uint16_t>(x + r),
              static_cast<std::uint16_t>(y + h - 1u),
              static_cast<std::uint16_t>(w - 2u * r), 1u, colour);
    fill_rect(x, static_cast<std::uint16_t>(y + r),
              1u, static_cast<std::uint16_t>(h - 2u * r), colour);
    fill_rect(static_cast<std::uint16_t>(x + w - 1u),
              static_cast<std::uint16_t>(y + r),
              1u, static_cast<std::uint16_t>(h - 2u * r), colour);

    // Corners via quarter-circle.
    auto corner = [&](std::int16_t ccx, std::int16_t ccy,
                      bool qx, bool qy) {
        std::int16_t cx2 = r, cy2 = 0;
        std::int16_t err2 = 0;
        while (cx2 >= cy2) {
            auto plot_q = [&](std::int16_t dx, std::int16_t dy) {
                const std::int16_t px = static_cast<std::int16_t>(ccx + (qx ? dx : -dx));
                const std::int16_t py = static_cast<std::int16_t>(ccy + (qy ? dy : -dy));
                if (px >= 0 && px < static_cast<std::int16_t>(kDisplayWidth) &&
                    py >= 0 && py < static_cast<std::int16_t>(kDisplayHeight)) {
                    put_pixel(static_cast<std::uint16_t>(px),
                              static_cast<std::uint16_t>(py), colour);
                }
            };
            plot_q(cx2, cy2);
            plot_q(cy2, cx2);
            ++cy2;
            if (err2 <= 0) {
                err2 = static_cast<std::int16_t>(err2 + 2 * cy2 + 1);
            } else {
                --cx2;
                err2 = static_cast<std::int16_t>(err2 + 2 * (cy2 - cx2) + 1);
            }
        }
    };

    corner(static_cast<std::int16_t>(x + r),         static_cast<std::int16_t>(y + r),         false, false);
    corner(static_cast<std::int16_t>(x + w - 1u - r), static_cast<std::int16_t>(y + r),         true,  false);
    corner(static_cast<std::int16_t>(x + r),         static_cast<std::int16_t>(y + h - 1u - r), false, true);
    corner(static_cast<std::int16_t>(x + w - 1u - r), static_cast<std::int16_t>(y + h - 1u - r), true, true);
}

void Renderer::draw_arc(std::int16_t cx, std::int16_t cy, std::int16_t radius,
                         std::int16_t a0, std::int16_t a1,
                         std::uint16_t colour) {
    // Walk in 1-degree steps using the fixed-point sin/cos table.
    // For smooth arcs a 0.5-degree step would be better; 1° gives visible
    // gaps at large radii.  This is sufficient for progress arcs at watch scale.
    std::int16_t deg = a0;
    while (deg != a1) {
        const std::int32_t px = cx + (icos(deg) * radius) / 256;
        const std::int32_t py = cy - (isin(deg) * radius) / 256;
        if (px >= 0 && px < static_cast<std::int32_t>(kDisplayWidth) &&
            py >= 0 && py < static_cast<std::int32_t>(kDisplayHeight)) {
            put_pixel(static_cast<std::uint16_t>(px),
                      static_cast<std::uint16_t>(py), colour);
        }
        deg = static_cast<std::int16_t>((deg + 1) % 360);
    }
}

void Renderer::blit_rgb565(std::uint16_t x, std::uint16_t y,
                             std::uint16_t w, std::uint16_t h,
                             const std::uint16_t* src) {
    for (std::uint16_t dy = 0u; dy < h; ++dy) {
        for (std::uint16_t dx = 0u; dx < w; ++dx) {
            put_pixel(static_cast<std::uint16_t>(x + dx),
                      static_cast<std::uint16_t>(y + dy),
                      src[dy * w + dx]);
        }
    }
}

void Renderer::blit_rgb332(std::uint16_t x, std::uint16_t y,
                             std::uint16_t w, std::uint16_t h,
                             const std::uint8_t* src) {
    for (std::uint16_t dy = 0u; dy < h; ++dy) {
        for (std::uint16_t dx = 0u; dx < w; ++dx) {
            put_pixel(static_cast<std::uint16_t>(x + dx),
                      static_cast<std::uint16_t>(y + dy),
                      rgb332_to_565(src[dy * w + dx]));
        }
    }
}

void Renderer::blit_1bit(std::uint16_t x, std::uint16_t y,
                           std::uint16_t w, std::uint16_t h,
                           const std::uint8_t* src,
                           std::uint16_t fg, std::uint16_t bg) {
    for (std::uint16_t dy = 0u; dy < h; ++dy) {
        for (std::uint16_t dx = 0u; dx < w; ++dx) {
            const std::uint32_t bit_idx = dy * w + dx;
            const bool set = (src[bit_idx / 8u] >> (7u - (bit_idx % 8u))) & 1u;
            put_pixel(static_cast<std::uint16_t>(x + dx),
                      static_cast<std::uint16_t>(y + dy),
                      set ? fg : bg);
        }
    }
}

std::uint32_t Renderer::flush() {
    // Prefer M3 path: caller uses set_tile_filter + flush_filtered_tile per tile.
    // This legacy flush still transmits buf_[0] for every dirty bit (M1 demos).

    std::uint32_t tiles_sent = 0u;

    for (std::uint32_t t = 0u; t < kTileCount; ++t) {
        if (!dirty_.is_dirty(t)) {
            continue;
        }

        const std::uint16_t ty0 = static_cast<std::uint16_t>(t * kTileHeight);
        const std::uint16_t ty1 =
            static_cast<std::uint16_t>(ty0 + kTileHeight - 1u);

        st7789::set_window(0u, ty0,
                           static_cast<std::uint16_t>(kDisplayWidth - 1u), ty1);
        if (!st7789::write_pixels(buf_[0], kTileBytes)) {
            // Keep dirty for retry on the next flush.
            continue;
        }
        dirty_.clear_tile(t);
        ++tiles_sent;
    }

    return tiles_sent;
}

bool Renderer::in_clip(std::uint16_t x, std::uint16_t y) const {
    if (clip_w_ == 0u) {
        return true;
    }
    return x >= clip_x_ && x < static_cast<std::uint16_t>(clip_x_ + clip_w_) &&
           y >= clip_y_ && y < static_cast<std::uint16_t>(clip_y_ + clip_h_);
}

void Renderer::set_tile_filter(std::int32_t tile_row) {
    tile_filter_ = tile_row;
}

void Renderer::set_clip(std::uint16_t x, std::uint16_t y,
                        std::uint16_t w, std::uint16_t h) {
    clip_x_ = x;
    clip_y_ = y;
    clip_w_ = w;
    clip_h_ = h;
}

void Renderer::clear_clip() {
    clip_w_ = 0u;
    clip_h_ = 0u;
}

void Renderer::clear_tile_buffer(std::uint16_t colour) {
    const std::uint8_t hi = static_cast<std::uint8_t>(colour >> 8u);
    const std::uint8_t lo = static_cast<std::uint8_t>(colour);
    for (std::uint32_t i = 0u; i < kTileBytes / 2u; ++i) {
        buf_[0][i * 2u] = hi;
        buf_[0][i * 2u + 1u] = lo;
    }
}

void Renderer::flush_filtered_tile() {
    if (tile_filter_ < 0) {
        return;
    }
    const std::uint32_t t = static_cast<std::uint32_t>(tile_filter_);
    if (t >= kTileCount) {
        return;
    }
    const std::uint16_t ty0 = static_cast<std::uint16_t>(t * kTileHeight);
    const std::uint16_t ty1 = static_cast<std::uint16_t>(ty0 + kTileHeight - 1u);
    st7789::set_window(0u, ty0,
                       static_cast<std::uint16_t>(kDisplayWidth - 1u), ty1);
    if (!st7789::write_pixels(buf_[0], kTileBytes)) {
        dirty_.mark_tile(t);
        return;
    }
    dirty_.clear_tile(t);
}
