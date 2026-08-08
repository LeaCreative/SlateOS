// Which tiles does a push actually write to the panel?
//
// Written to settle a question that three rounds of code reading could not:
// the operator sees the watch face vanish for a moment, occasionally. The
// candidate mechanism is that a *band-only* display list — the clock band, the
// diagnostic band, the OTA banner — is pushed as a whole retained list, and
// `render_retained_to_display()` then walks all 30 tiles, clears each to black
// and flushes it unconditionally. If that is what happens, every band push
// blanks the 25 tiles the band does not cover, and the face only comes back on
// the next full repaint.
//
// It is cheap to answer here and expensive to answer on a sealed watch, which
// is the whole argument for this file existing.

#include "sdp_interpreter.hpp"
#include "sdp_opcodes.hpp"
#include "renderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

// Every tile the display was asked to accept, and whether the bytes were all
// black. Recorded from the stub below, which stands in for the panel.
struct FlushRecord {
  bool written[kTileCount] = {};
  bool all_black[kTileCount] = {};
  std::uint32_t flush_count = 0u;
};

FlushRecord g_rec;
std::uint16_t g_window_y0 = 0u;

void expect(const char* name, bool cond) {
  if (!cond) {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

}  // namespace

namespace st7789 {
void set_window(std::uint16_t, std::uint16_t y0, std::uint16_t, std::uint16_t) {
  g_window_y0 = y0;
}
bool write_pixels(const std::uint8_t* data, std::uint32_t len) {
  const std::uint32_t tile = g_window_y0 / kTileHeight;
  if (tile < kTileCount) {
    g_rec.written[tile] = true;
    bool black = true;
    for (std::uint32_t i = 0u; i < len; ++i) {
      if (data[i] != 0u) {
        black = false;
        break;
      }
    }
    g_rec.all_black[tile] = black;
  }
  ++g_rec.flush_count;
  return true;
}
}  // namespace st7789

// The interpreter's side effects are irrelevant here — this test only asks
// which tiles reached the panel.
namespace input {
struct HitRect;
void set_hit_rects(const HitRect*, std::size_t) {}
}  // namespace input
namespace slate {
namespace wdt {
void pet_service() {}
}  // namespace wdt
namespace motor {
void play(std::uint8_t) {}
}  // namespace motor
}  // namespace slate
namespace backlight {
void set(unsigned int) {}
}  // namespace backlight
namespace board {
std::uint32_t micros() { return 0u; }
}  // namespace board

namespace {

// A band-only list, shaped exactly like ui::build_clock_band: a black fill over
// one horizontal strip, a palette entry, a coloured rect inside it, COMMIT. No
// CLEAR, because a band is meant to leave the rest of the screen alone.
std::size_t build_band(std::uint8_t* out, std::uint8_t y, std::uint8_t h) {
  std::size_t n = 0u;
  // RECT x,y,w,h, colour tag (literal RGB565), style FILL — the strip itself.
  out[n++] = sdp::op::RECT;
  out[n++] = 0u;
  out[n++] = y;
  out[n++] = 240u;
  out[n++] = h;
  out[n++] = 0x00u;  // literal colour follows
  out[n++] = 0x00u;
  out[n++] = 0x00u;  // black
  out[n++] = 0x00u;  // style: fill
  // A white rect inside the band, so "the band drew something" is detectable.
  out[n++] = sdp::op::RECT;
  out[n++] = 10u;
  out[n++] = static_cast<std::uint8_t>(y + 2u);
  out[n++] = 100u;
  out[n++] = static_cast<std::uint8_t>(h - 4u);
  out[n++] = 0x00u;
  out[n++] = 0xFFu;
  out[n++] = 0xFFu;
  out[n++] = 0x00u;
  out[n++] = sdp::op::COMMIT;
  out[n++] = 0x00u;
  return n;
}

/**
 * The measurement. A band covering y=56..96 touches tiles 7..11 of 30.
 *
 * If only those tiles are flushed, band pushes are safe and the flash has some
 * other cause. If all 30 are flushed with 25 of them black, a band push blanks
 * the face and the mechanism is found.
 */
void test_band_push_only_touches_its_own_tiles() {
  Renderer r;
  sdp::Interpreter interp;
  interp.init(&r);

  std::uint8_t band[64];
  const std::size_t n = build_band(band, 56u, 40u);

  g_rec = FlushRecord{};
  const sdp::PushStats s = interp.push_list(band, n);
  expect("band list is accepted", s.status == sdp::Status::Ok);

  std::uint32_t written = 0u;
  std::uint32_t blanked = 0u;
  for (std::uint32_t t = 0u; t < kTileCount; ++t) {
    if (g_rec.written[t]) ++written;
    if (g_rec.written[t] && g_rec.all_black[t]) ++blanked;
  }
  std::printf("  band y=56..96 -> %u of %u tiles flushed, %u of them all-black\n",
              written, static_cast<std::uint32_t>(kTileCount), blanked);

  // The band spans tiles 7..11 inclusive (56/8 .. 95/8).
  expect("tiles covering the band are written", g_rec.written[7] && g_rec.written[11]);

  // THE question. A tile outside the band being flushed all-black means the
  // push wiped screen content that nothing intended to change.
  expect("a band push does not blank tiles outside the band",
         blanked == 0u);
}

/** A full-screen list must still reach every tile — the culling fast path. */
void test_full_screen_push_touches_every_tile() {
  Renderer r;
  sdp::Interpreter interp;
  interp.init(&r);

  std::uint8_t list[32];
  std::size_t n = 0u;
  list[n++] = sdp::op::CLEAR;
  list[n++] = 0x00u;
  list[n++] = 0x00u;
  list[n++] = 0x00u;
  list[n++] = sdp::op::COMMIT;
  list[n++] = 0x00u;

  g_rec = FlushRecord{};
  (void)interp.push_list(list, n);
  std::uint32_t written = 0u;
  for (std::uint32_t t = 0u; t < kTileCount; ++t) {
    if (g_rec.written[t]) ++written;
  }
  expect("a full-screen CLEAR reaches all 30 tiles", written == kTileCount);
}

}  // namespace

int main() {
  test_band_push_only_touches_its_own_tiles();
  test_full_screen_push_touches_every_tile();
  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all band-push tile tests passed\n");
  return 0;
}
