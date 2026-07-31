#pragma once

#include "sdp_validate.hpp"

#include <cstddef>
#include <cstdint>

namespace sdp {

struct ParseOptions {
  std::uint32_t max_ops   = kMaxOps;
  std::uint32_t max_bytes = kMaxListBytes;
  bool execute            = false;  // false = validate-only (fuzzer / dry-run)
};

struct ParseResult {
  Status status = Status::Ok;
  std::uint32_t ops_consumed = 0u;
  std::size_t bytes_consumed = 0u;
  bool saw_commit = false;
  std::uint8_t commit_flags = 0u;
  std::uint16_t retain_ttl_s = 0u;
  bool saw_retain = false;
};

// Optional execution sink.  Nullptr + execute=false → validation walk only.
// All coordinate/colour fields have already been validated when callbacks fire.
// Stack-allocated only — no polymorphic delete.
struct Sink {
  virtual void clear(std::uint16_t color) { (void)color; }
  virtual void set_palette(std::uint8_t idx, std::uint16_t rgb565) {
    (void)idx; (void)rgb565;
  }
  virtual void rect(std::uint8_t x, std::uint8_t y, std::uint8_t w, std::uint8_t h,
                    std::uint16_t color, Style st) {
    (void)x; (void)y; (void)w; (void)h; (void)color; (void)st;
  }
  virtual void rect_round(std::uint8_t x, std::uint8_t y, std::uint8_t w,
                          std::uint8_t h, std::uint8_t r, std::uint16_t color,
                          Style st) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)color; (void)st;
  }
  virtual void line(std::uint8_t x0, std::uint8_t y0, std::uint8_t x1,
                    std::uint8_t y1, std::uint16_t color, std::uint8_t width) {
    (void)x0; (void)y0; (void)x1; (void)y1; (void)color; (void)width;
  }
  virtual void circle(std::uint8_t cx, std::uint8_t cy, std::uint8_t r,
                      std::uint16_t color, Style st) {
    (void)cx; (void)cy; (void)r; (void)color; (void)st;
  }
  virtual void arc(std::uint8_t cx, std::uint8_t cy, std::uint8_t r,
                   std::uint16_t a0, std::uint16_t a1, std::uint16_t color,
                   std::uint8_t width) {
    (void)cx; (void)cy; (void)r; (void)a0; (void)a1; (void)color; (void)width;
  }
  virtual void polyline(std::uint8_t count, std::uint16_t color,
                        std::uint8_t width, const std::uint8_t* xy_pairs) {
    (void)count; (void)color; (void)width; (void)xy_pairs;
  }
  virtual void clip_rect(std::uint8_t x, std::uint8_t y, std::uint8_t w,
                         std::uint8_t h) {
    (void)x; (void)y; (void)w; (void)h;
  }
  virtual void clip_clear() {}
  virtual void text(std::uint8_t font, std::uint8_t x, std::uint8_t y,
                    std::uint16_t color, std::uint8_t align, std::uint8_t len,
                    const std::uint8_t* utf8) {
    (void)font; (void)x; (void)y; (void)color; (void)align; (void)len; (void)utf8;
  }
  // Defaults to native-size text so sinks that predate TEXT_SCALED still draw
  // something legible rather than nothing.
  virtual void text_scaled(std::uint8_t font, std::uint8_t x, std::uint8_t y,
                           std::uint16_t color, std::uint8_t align,
                           std::uint8_t scale, std::uint8_t len,
                           const std::uint8_t* utf8) {
    (void)scale;
    text(font, x, y, color, align, len, utf8);
  }
  virtual void text_box(std::uint8_t font, std::uint8_t x, std::uint8_t y,
                        std::uint8_t w, std::uint8_t h, std::uint16_t color,
                        std::uint8_t align, std::uint8_t flags, std::uint8_t len,
                        const std::uint8_t* utf8) {
    (void)font; (void)x; (void)y; (void)w; (void)h; (void)color; (void)align;
    (void)flags; (void)len; (void)utf8;
  }
  virtual void icon(std::uint8_t atlas, std::uint16_t id, std::uint8_t x,
                    std::uint8_t y, std::uint16_t tint) {
    (void)atlas; (void)id; (void)x; (void)y; (void)tint;
  }
  virtual void image(std::uint8_t asset, std::uint16_t id, std::uint8_t x,
                     std::uint8_t y) {
    (void)asset; (void)id; (void)x; (void)y;
  }
  virtual void progress_bar(std::uint8_t x, std::uint8_t y, std::uint8_t w,
                            std::uint8_t h, std::uint8_t pct, std::uint16_t fg,
                            std::uint16_t bg) {
    (void)x; (void)y; (void)w; (void)h; (void)pct; (void)fg; (void)bg;
  }
  virtual void progress_arc(std::uint8_t cx, std::uint8_t cy, std::uint8_t r,
                            std::uint8_t pct, std::uint16_t fg, std::uint16_t bg,
                            std::uint8_t width) {
    (void)cx; (void)cy; (void)r; (void)pct; (void)fg; (void)bg; (void)width;
  }
  virtual void begin_elem(std::uint16_t id, std::uint8_t x, std::uint8_t y,
                          std::uint8_t w, std::uint8_t h, std::uint8_t flags) {
    (void)id; (void)x; (void)y; (void)w; (void)h; (void)flags;
  }
  virtual void end_elem() {}
  virtual void scroll_region(std::uint8_t y, std::uint8_t h,
                             std::uint16_t content_h) {
    (void)y; (void)h; (void)content_h;
  }
  virtual void patch(std::uint8_t slot, std::uint8_t x, std::uint8_t y,
                     std::uint8_t w, std::uint8_t h, std::uint8_t format,
                     std::uint8_t encoding, std::uint16_t len,
                     const std::uint8_t* data) {
    (void)slot; (void)x; (void)y; (void)w; (void)h; (void)format;
    (void)encoding; (void)len; (void)data;
  }
  virtual void patch_ref(std::uint8_t slot, std::uint8_t x, std::uint8_t y) {
    (void)slot; (void)x; (void)y;
  }
  virtual void haptic(std::uint8_t pattern) { (void)pattern; }
  virtual void backlight(std::uint8_t level) { (void)level; }
  virtual void commit(std::uint8_t flags) { (void)flags; }
  virtual void retain(std::uint16_t ttl_s) { (void)ttl_s; }
};

// Walk an untrusted display list.  Never faults; never reads out of bounds.
// On Reject/Truncated the caller must leave the previous frame intact.
ParseResult parse(const std::uint8_t* data, std::size_t size,
                  const ParseOptions& opt, Sink* sink);

}  // namespace sdp
