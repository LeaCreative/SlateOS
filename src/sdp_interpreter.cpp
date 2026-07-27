#include "sdp_interpreter.hpp"

#include "backlight.hpp"
#include "board.hpp"
#include "font_builtin.hpp"
#include "input_event.hpp"

#include <cstring>

namespace sdp {
namespace {

void draw_char(Renderer* r, std::uint16_t x, std::uint16_t y, char c,
               std::uint16_t color) {
  using namespace font::builtin3x5;
  const int cp = static_cast<int>(c);
  if (cp < static_cast<int>(kFirstCodepoint) ||
      cp >= static_cast<int>(kFirstCodepoint + kGlyphCount)) {
    // Unknown glyph: 3×5 block placeholder (matches emulator).
    r->fill_rect(x, y, kCellWidth, kCellHeight, color);
    return;
  }
  const std::uint8_t* g = kRows[static_cast<std::uint8_t>(cp - kFirstCodepoint)];
  for (std::uint16_t row = 0u; row < kCellHeight; ++row) {
    for (std::uint16_t col = 0u; col < kCellWidth; ++col) {
      if (g[row] & (1u << (kCellWidth - 1u - col))) {
        r->fill_rect(static_cast<std::uint16_t>(x + col),
                     static_cast<std::uint16_t>(y + row), 1u, 1u, color);
      }
    }
  }
}

void draw_text_run(Renderer* r, std::uint8_t x, std::uint8_t y,
                   std::uint16_t color, std::uint8_t al, std::uint8_t len,
                   const std::uint8_t* utf8) {
  const std::uint16_t pixel_w =
      static_cast<std::uint16_t>(len) * font::builtin3x5::kAdvance;
  std::uint16_t start_x = x;
  if (al == align::CENTER && pixel_w / 2u < x) {
    start_x = static_cast<std::uint16_t>(x - pixel_w / 2u);
  } else if (al == align::CENTER) {
    start_x = 0u;
  } else if (al == align::RIGHT) {
    start_x = (pixel_w < x) ? static_cast<std::uint16_t>(x - pixel_w) : 0u;
  }
  for (std::uint8_t i = 0u; i < len; ++i) {
    draw_char(r,
              static_cast<std::uint16_t>(start_x + i * font::builtin3x5::kAdvance),
              y, static_cast<char>(utf8[i]), color);
  }
}

void stroke_rect(Renderer* r, std::uint8_t x, std::uint8_t y, std::uint8_t w,
                 std::uint8_t h, std::uint16_t color, std::uint8_t width) {
  for (std::uint8_t i = 0u; i < width; ++i) {
    if (w <= 2u * i || h <= 2u * i) break;
    r->draw_round_rect(static_cast<std::uint16_t>(x + i),
                       static_cast<std::uint16_t>(y + i),
                       static_cast<std::uint16_t>(w - 2u * i),
                       static_cast<std::uint16_t>(h - 2u * i),
                       0u, color);
  }
}

void apply_style_rect(Renderer* r, std::uint8_t x, std::uint8_t y,
                      std::uint8_t w, std::uint8_t h, std::uint16_t color,
                      Style st) {
  if (st.mode == style::ModeFill || st.mode == style::ModeFillStroke) {
    r->fill_rect(x, y, w, h, color);
  }
  if (st.mode == style::ModeStroke || st.mode == style::ModeFillStroke) {
    stroke_rect(r, x, y, w, h, color, st.width);
  }
}

// Collects hit-test elems + scroll meta during an execute pass (no draw).
struct MetaSink : Sink {
  input::HitRect* hits = nullptr;
  std::size_t* hit_count = nullptr;
  std::size_t hit_cap = 0u;

  struct Frame {
    std::uint16_t id = 0u;
    std::uint8_t x = 0u, y = 0u, w = 0u, h = 0u;
    std::uint8_t flags = 0u;
  };
  Frame stack[kMaxElemDepth] = {};
  std::uint32_t depth = 0u;

  std::uint8_t* scroll_y = nullptr;
  std::uint8_t* scroll_h = nullptr;
  std::uint16_t* scroll_content_h = nullptr;
  bool* have_scroll = nullptr;

  void begin_elem(std::uint16_t id, std::uint8_t x, std::uint8_t y,
                  std::uint8_t w, std::uint8_t h, std::uint8_t flags) override {
    if (depth >= kMaxElemDepth) return;
    stack[depth++] = Frame{id, x, y, w, h, flags};
  }

  void end_elem() override {
    if (depth == 0u) return;
    const Frame f = stack[--depth];
    if ((f.flags & elem_flags::NO_HIT) != 0u) return;
    if (hits == nullptr || hit_count == nullptr) return;
    if (*hit_count >= hit_cap) return;
    hits[*hit_count] = input::HitRect{f.id, f.x, f.y, f.w, f.h};
    // DISABLED stays in the table for layout; input layer may filter later.
    ++(*hit_count);
  }

  void scroll_region(std::uint8_t y, std::uint8_t h,
                     std::uint16_t content_h) override {
    if (scroll_y) *scroll_y = y;
    if (scroll_h) *scroll_h = h;
    if (scroll_content_h) *scroll_content_h = content_h;
    if (have_scroll) *have_scroll = true;
  }
};

// Draws into Renderer for one filtered tile.
struct DrawSink : Sink {
  Renderer* r = nullptr;
  std::uint16_t clear_color = 0u;
  bool cleared = false;

  std::uint8_t scroll_y = 0u;
  std::uint8_t scroll_h = 0u;
  std::uint16_t scroll_offset = 0u;
  bool in_scroll = false;

  std::uint8_t map_y(std::uint8_t y) const {
    if (!in_scroll) return y;
    const int screen =
        static_cast<int>(scroll_y) + static_cast<int>(y) -
        static_cast<int>(scroll_offset);
    if (screen < 0) return 0u;
    if (screen > 239) return 239u;
    return static_cast<std::uint8_t>(screen);
  }

  void clear(std::uint16_t color) override {
    clear_color = color;
    cleared = true;
    r->clear_tile_buffer(color);
  }

  void rect(std::uint8_t x, std::uint8_t y, std::uint8_t w, std::uint8_t h,
            std::uint16_t color, Style st) override {
    apply_style_rect(r, x, map_y(y), w, h, color, st);
  }

  void rect_round(std::uint8_t x, std::uint8_t y, std::uint8_t w, std::uint8_t h,
                  std::uint8_t rad, std::uint16_t color, Style st) override {
    const std::uint8_t yy = map_y(y);
    if (st.mode == style::ModeFill || st.mode == style::ModeFillStroke) {
      r->fill_rect(x, yy, w, h, color);
    }
    if (st.mode == style::ModeStroke || st.mode == style::ModeFillStroke) {
      r->draw_round_rect(x, yy, w, h, rad, color);
    }
  }

  void line(std::uint8_t x0, std::uint8_t y0, std::uint8_t x1, std::uint8_t y1,
            std::uint16_t color, std::uint8_t width) override {
    (void)width;
    r->draw_line(x0, map_y(y0), x1, map_y(y1), color);
  }

  void circle(std::uint8_t cx, std::uint8_t cy, std::uint8_t rad,
              std::uint16_t color, Style st) override {
    const std::uint8_t yy = map_y(cy);
    if (st.mode == style::ModeFill || st.mode == style::ModeFillStroke) {
      for (std::uint8_t rr = 0u; rr <= rad; ++rr) {
        r->draw_circle(cx, yy, rr, color);
      }
    }
    if (st.mode == style::ModeStroke || st.mode == style::ModeFillStroke) {
      r->draw_circle(cx, yy, rad, color);
    }
  }

  void arc(std::uint8_t cx, std::uint8_t cy, std::uint8_t rad, std::uint16_t a0,
           std::uint16_t a1, std::uint16_t color, std::uint8_t width) override {
    (void)width;
    r->draw_arc(cx, map_y(cy), rad, static_cast<std::int16_t>(a0),
                static_cast<std::int16_t>(a1), color);
  }

  void polyline(std::uint8_t count, std::uint16_t color, std::uint8_t width,
                const std::uint8_t* xy) override {
    (void)width;
    for (std::uint8_t i = 1u; i < count; ++i) {
      r->draw_line(xy[(i - 1u) * 2u], map_y(xy[(i - 1u) * 2u + 1u]),
                   xy[i * 2u], map_y(xy[i * 2u + 1u]), color);
    }
  }

  void clip_rect(std::uint8_t x, std::uint8_t y, std::uint8_t w,
                 std::uint8_t h) override {
    r->set_clip(x, map_y(y), w, h);
  }

  void clip_clear() override {
    r->clear_clip();
    in_scroll = false;
  }

  void text(std::uint8_t font, std::uint8_t x, std::uint8_t y,
            std::uint16_t color, std::uint8_t al, std::uint8_t len,
            const std::uint8_t* utf8) override {
    (void)font;
    draw_text_run(r, x, map_y(y), color, al, len, utf8);
  }

  void text_box(std::uint8_t font, std::uint8_t x, std::uint8_t y,
                std::uint8_t w, std::uint8_t h, std::uint16_t color,
                std::uint8_t al, std::uint8_t flags, std::uint8_t len,
                const std::uint8_t* utf8) override {
    (void)font; (void)w; (void)h; (void)flags;
    draw_text_run(r, x, map_y(y), color, al, len, utf8);
  }

  void icon(std::uint8_t atlas, std::uint16_t id, std::uint8_t x, std::uint8_t y,
            std::uint16_t tint) override {
    (void)atlas; (void)id;
    r->fill_rect(x, map_y(y), 8u, 8u, tint);
  }

  void image(std::uint8_t asset, std::uint16_t id, std::uint8_t x,
             std::uint8_t y) override {
    (void)asset; (void)id;
    r->fill_rect(x, map_y(y), 16u, 16u, 0x7BEFu);
  }

  void progress_bar(std::uint8_t x, std::uint8_t y, std::uint8_t w,
                    std::uint8_t h, std::uint8_t pct, std::uint16_t fg,
                    std::uint16_t bg) override {
    const std::uint8_t yy = map_y(y);
    r->fill_rect(x, yy, w, h, bg);
    const std::uint16_t fw =
        static_cast<std::uint16_t>((static_cast<std::uint32_t>(w) * pct) / 100u);
    if (fw > 0u) {
      r->fill_rect(x, yy, static_cast<std::uint16_t>(fw), h, fg);
    }
  }

  void progress_arc(std::uint8_t cx, std::uint8_t cy, std::uint8_t rad,
                    std::uint8_t pct, std::uint16_t fg, std::uint16_t bg,
                    std::uint8_t width) override {
    (void)width;
    const std::uint8_t yy = map_y(cy);
    r->draw_arc(cx, yy, rad, 0, 359, bg);
    const std::int16_t a1 =
        static_cast<std::int16_t>((360 * static_cast<int>(pct)) / 100);
    if (a1 > 0) {
      r->draw_arc(cx, yy, rad, 0, a1, fg);
    }
  }

  void patch(std::uint8_t slot, std::uint8_t x, std::uint8_t y, std::uint8_t w,
             std::uint8_t h, std::uint8_t format, std::uint8_t encoding,
             std::uint16_t len, const std::uint8_t* data) override {
    (void)slot;
    const std::uint8_t yy = map_y(y);
    if (encoding != patch_encoding::RAW) return;
    if (format == patch_format::RGB565) {
      const std::uint32_t need =
          static_cast<std::uint32_t>(w) * h * 2u;
      if (len < need) return;
      for (std::uint16_t row = 0u; row < h; ++row) {
        for (std::uint16_t col = 0u; col < w; ++col) {
          const std::uint32_t i =
              (static_cast<std::uint32_t>(row) * w + col) * 2u;
          const std::uint16_t pix = static_cast<std::uint16_t>(
              data[i] | (static_cast<std::uint16_t>(data[i + 1u]) << 8u));
          r->fill_rect(static_cast<std::uint16_t>(x + col),
                       static_cast<std::uint16_t>(yy + row), 1u, 1u, pix);
        }
      }
    } else if (format == patch_format::RGB332) {
      if (len < static_cast<std::uint32_t>(w) * h) return;
      r->blit_rgb332(x, yy, w, h, data);
    } else if (format == patch_format::MONO1) {
      const std::uint32_t need =
          (static_cast<std::uint32_t>(w) * h + 7u) / 8u;
      if (len < need) return;
      r->blit_1bit(x, yy, w, h, data, 0xFFFFu, 0x0000u);
    }
  }

  void patch_ref(std::uint8_t slot, std::uint8_t x, std::uint8_t y) override {
    (void)slot; (void)x; (void)y;
  }

  void haptic(std::uint8_t pattern) override {
    (void)pattern;
    // Side effects run once after COMMIT, not during per-tile replay.
  }

  void backlight(std::uint8_t level) override {
    (void)level;
  }

  void scroll_region(std::uint8_t y, std::uint8_t h,
                     std::uint16_t content_h) override {
    (void)content_h;
    scroll_y = y;
    scroll_h = h;
    in_scroll = true;
    r->set_clip(0u, y, static_cast<std::uint16_t>(kDisplaySize), h);
  }
};

struct SideEffectSink : Sink {
  void haptic(std::uint8_t pattern) override {
    std::uint32_t ms = 30u;
    switch (pattern) {
      case haptic_pattern::TICK:   ms = 20u; break;
      case haptic_pattern::SHORT:  ms = 40u; break;
      case haptic_pattern::DOUBLE: ms = 40u; break;
      case haptic_pattern::LONG:   ms = 120u; break;
      case haptic_pattern::ERROR:  ms = 80u; break;
      default: break;
    }
    board::pulse_motor(ms);
    if (pattern == haptic_pattern::DOUBLE) {
      board::busy_wait_ms(40u);
      board::pulse_motor(ms);
    }
  }

  void backlight(std::uint8_t level) override {
    ::backlight::set(level);
  }
};

}  // namespace

void Interpreter::init(Renderer* renderer) {
  renderer_ = renderer;
  retained_len_ = 0u;
  hit_count_ = 0u;
  have_scroll_ = false;
  scroll_offset_ = 0u;
}

PushStats Interpreter::push_list(const std::uint8_t* data, std::size_t size) {
  PushStats stats{};

  if (renderer_ == nullptr || data == nullptr) {
    stats.status = Status::Reject;
    return stats;
  }
  if (size > kMaxListBytes) {
    stats.status = Status::Reject;
    return stats;
  }

  const std::uint32_t t0 = board::micros();

  // Pass 1: validate only — never touch retained or display on failure.
  ParseOptions vopt;
  vopt.max_ops = kMaxOps;
  vopt.max_bytes = kMaxListBytes;
  vopt.execute = false;
  const ParseResult vr = parse(data, size, vopt, nullptr);
  stats.ops = vr.ops_consumed;
  stats.bytes = vr.bytes_consumed;
  stats.parse_us = board::micros() - t0;

  if (vr.status != Status::Ok || !vr.saw_commit) {
    stats.status = (vr.status == Status::Ok) ? Status::Reject : vr.status;
    return stats;
  }

  // Promote to retained only after successful validation + COMMIT.
  std::memcpy(staging_, data, size);
  std::memcpy(retained_, staging_, size);
  retained_len_ = size;
  if (vr.saw_retain) {
    retain_ttl_s_ = vr.retain_ttl_s;
  }

  rebuild_hits_and_scroll_meta();
  input::set_hit_rects(hit_rects_, hit_count_);

  {
    SideEffectSink effects;
    ParseOptions opt;
    opt.execute = true;
    (void)parse(retained_, retained_len_, opt, &effects);
  }

  const std::uint32_t t1 = board::micros();
  render_retained_to_display();
  stats.render_us = board::micros() - t1;
  stats.status = Status::Ok;

  // 30 ms budget: log-only if exceeded (list already validated/retained).
  if (stats.parse_us + stats.render_us > 30000u) {
    // Soft budget — retained is already committed; next push still works.
  }

  return stats;
}

void Interpreter::rebuild_hits_and_scroll_meta() {
  hit_count_ = 0u;
  have_scroll_ = false;
  scroll_y_ = 0u;
  scroll_h_ = 0u;
  scroll_content_h_ = 0u;

  MetaSink meta;
  meta.hits = hit_rects_;
  meta.hit_count = &hit_count_;
  meta.hit_cap = kMaxHitElems;
  meta.scroll_y = &scroll_y_;
  meta.scroll_h = &scroll_h_;
  meta.scroll_content_h = &scroll_content_h_;
  meta.have_scroll = &have_scroll_;

  ParseOptions opt;
  opt.execute = true;
  (void)parse(retained_, retained_len_, opt, &meta);

  if (have_scroll_) {
    const std::uint16_t max_off =
        (scroll_content_h_ > scroll_h_)
            ? static_cast<std::uint16_t>(scroll_content_h_ - scroll_h_)
            : 0u;
    if (scroll_offset_ > max_off) {
      scroll_offset_ = max_off;
    }
  }
}

void Interpreter::render_retained_to_display() {
  if (renderer_ == nullptr || retained_len_ == 0u) return;

  DrawSink draw;
  draw.r = renderer_;
  draw.scroll_offset = scroll_offset_;

  for (std::uint32_t t = 0u; t < kTileCount; ++t) {
    renderer_->set_tile_filter(static_cast<std::int32_t>(t));
    renderer_->clear_clip();
    renderer_->clear_tile_buffer(0x0000u);
    draw.in_scroll = false;

    ParseOptions opt;
    opt.execute = true;
    (void)parse(retained_, retained_len_, opt, &draw);

    renderer_->flush_filtered_tile();
  }

  renderer_->set_tile_filter(Renderer::kNoTileFilter);
  renderer_->clear_clip();
}

void Interpreter::rerender_retained() {
  render_retained_to_display();
}

void Interpreter::set_scroll_offset(std::uint16_t offset) {
  if (!have_scroll_) {
    scroll_offset_ = 0u;
    return;
  }
  const std::uint16_t max_off =
      (scroll_content_h_ > scroll_h_)
          ? static_cast<std::uint16_t>(scroll_content_h_ - scroll_h_)
          : 0u;
  scroll_offset_ = (offset > max_off) ? max_off : offset;
  rerender_retained();
}

}  // namespace sdp
