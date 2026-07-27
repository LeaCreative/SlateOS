#include "backlight.hpp"
#include "board.hpp"
#include "button.hpp"
#include "cst816s.hpp"
#include "input_event.hpp"
#include "renderer.hpp"
#include "rtt.hpp"
#include "spi_bus.hpp"
#include "st7789.hpp"
#include "twi.hpp"

#include <cstdint>

// ── Tiny 3×5 digit font (bit0 = leftmost column of each row) ─────────────────

static constexpr std::uint8_t kDigit3x5[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},  // 0
    {0x2, 0x2, 0x2, 0x2, 0x2},  // 1
    {0x7, 0x1, 0x7, 0x4, 0x7},  // 2
    {0x7, 0x1, 0x7, 0x1, 0x7},  // 3
    {0x5, 0x5, 0x7, 0x1, 0x1},  // 4
    {0x7, 0x4, 0x7, 0x1, 0x7},  // 5
    {0x7, 0x4, 0x7, 0x5, 0x7},  // 6
    {0x7, 0x1, 0x1, 0x1, 0x1},  // 7
    {0x7, 0x5, 0x7, 0x5, 0x7},  // 8
    {0x7, 0x5, 0x7, 0x1, 0x7},  // 9
};

static Renderer g_renderer;
static std::uint16_t g_last_x = 120u;
static std::uint16_t g_last_y = 120u;
static bool g_have_touch = false;

static const input::HitRect kDemoRects[] = {
    {1u, 10u,  180u, 60u, 40u},   // left soft button
    {2u, 90u,  180u, 60u, 40u},   // centre
    {3u, 170u, 180u, 60u, 40u},   // right
};

static void draw_digit(std::uint16_t x, std::uint16_t y, std::uint8_t digit,
                       std::uint16_t fg) {
  if (digit > 9u) {
    return;
  }
  // 1×1 pixels — 3×5 glyph fits in a single 8-px tile row.
  for (std::uint16_t row = 0u; row < 5u; ++row) {
    const std::uint8_t bits = kDigit3x5[digit][row];
    for (std::uint16_t col = 0u; col < 3u; ++col) {
      if (bits & (1u << (2u - col))) {
        g_renderer.fill_rect(static_cast<std::uint16_t>(x + col),
                             static_cast<std::uint16_t>(y + row),
                             1u, 1u, fg);
      }
    }
  }
}

static void draw_u16(std::uint16_t x, std::uint16_t y, std::uint16_t value,
                     std::uint16_t fg) {
  draw_digit(x, y, static_cast<std::uint8_t>((value / 100u) % 10u), fg);
  draw_digit(static_cast<std::uint16_t>(x + 5u), y,
             static_cast<std::uint8_t>((value / 10u) % 10u), fg);
  draw_digit(static_cast<std::uint16_t>(x + 10u), y,
             static_cast<std::uint8_t>(value % 10u), fg);
}

static void redraw_scene() {
  // Tile-by-tile scene: dark background, hit targets, crosshair + coords.
  for (std::uint32_t t = 0u; t < kTileCount; ++t) {
    const std::uint16_t ty = static_cast<std::uint16_t>(t * kTileHeight);
    const std::uint16_t ty_end = static_cast<std::uint16_t>(ty + kTileHeight);

    g_renderer.fill_rect(0u, ty,
                         static_cast<std::uint16_t>(kDisplayWidth),
                         static_cast<std::uint16_t>(kTileHeight),
                         rgb888_to_565(8u, 10u, 18u));

    // Soft-button strip at y=180..219.
    if (ty < 220u && ty_end > 180u) {
      const std::uint16_t ry0 = (ty > 180u) ? ty : 180u;
      const std::uint16_t ry1 = (ty_end < 220u) ? ty_end : 220u;
      const std::uint16_t rh = static_cast<std::uint16_t>(ry1 - ry0);
      g_renderer.fill_rect(10u,  ry0, 60u, rh, rgb888_to_565(40u, 80u, 140u));
      g_renderer.fill_rect(90u,  ry0, 60u, rh, rgb888_to_565(40u, 120u, 80u));
      g_renderer.fill_rect(170u, ry0, 60u, rh, rgb888_to_565(140u, 60u, 40u));
    }

    // Crosshair around last touch (±8 px).
    if (g_have_touch) {
      const std::uint16_t cx = g_last_x;
      const std::uint16_t cy = g_last_y;
      const std::uint16_t h0 = (cx > 8u) ? static_cast<std::uint16_t>(cx - 8u) : 0u;
      const std::uint16_t colour = rgb888_to_565(255u, 220u, 40u);

      if (cy >= ty && cy < ty_end) {
        g_renderer.fill_rect(h0, cy, 17u, 1u, colour);
      }

      const std::uint16_t v0 = (cy > 8u) ? static_cast<std::uint16_t>(cy - 8u) : 0u;
      const std::uint16_t v1 = static_cast<std::uint16_t>(cy + 8u);
      if (ty < v1 && ty_end > v0) {
        const std::uint16_t ry0 = (ty > v0) ? ty : v0;
        const std::uint16_t ry1 = (ty_end < v1) ? ty_end : v1;
        g_renderer.fill_rect(cx, ry0, 1u,
                             static_cast<std::uint16_t>(ry1 - ry0), colour);
      }
    }

    // Coordinate readout — entirely within tile 0 (y=1..5).
    if (ty == 0u && g_have_touch) {
      draw_u16(8u, 1u, g_last_x, rgb888_to_565(220u, 220u, 230u));
      draw_u16(40u, 1u, g_last_y, rgb888_to_565(220u, 220u, 230u));
    }

    (void)g_renderer.flush();
  }
}

static void log_event(const input::Event& ev) {
  switch (ev.type) {
    case input::EventType::Tap:
      rtt::write("TAP x=");
      rtt::write_hex(ev.x);
      rtt::write(" y=");
      rtt::write_hex(ev.y);
      rtt::write(" elem=");
      rtt::write_hex(ev.element_id);
      rtt::write_line("");
      break;
    case input::EventType::LongPress:
      rtt::write("LONG_PRESS x=");
      rtt::write_hex(ev.x);
      rtt::write(" y=");
      rtt::write_hex(ev.y);
      rtt::write(" elem=");
      rtt::write_hex(ev.element_id);
      rtt::write_line("");
      break;
    case input::EventType::Swipe:
      rtt::write("SWIPE dir=");
      rtt::write_hex(static_cast<std::uint32_t>(ev.swipe));
      rtt::write_line("");
      break;
    case input::EventType::Button:
      rtt::write("BUTTON action=");
      rtt::write_hex(static_cast<std::uint32_t>(ev.button));
      rtt::write_line("");
      break;
    default:
      break;
  }
}

extern "C" int main() {
  rtt::init();
  rtt::log(rtt::Level::Info, "Slate M2 boot — touch + button + input events");

  for (int i = 0; i < 3; ++i) {
    board::pulse_motor(60u);
    board::busy_wait_ms(100u);
  }

  spi::init();
  st7789::init();
  backlight::init();
  backlight::set(50u);

  twi::init();
  cst816s::init();
  button::init();
  input::init();
  input::set_hit_rects(kDemoRects, 3u);

  rtt::log(rtt::Level::Info, "Input ready — tap / swipe / button");
  redraw_scene();

  std::uint32_t last_redraw_us = board::micros();

  while (true) {
    const input::Event ev = input::poll();
    if (ev.type == input::EventType::None) {
      // Idle poll cadence ~5 ms keeps button debounce responsive.
      board::busy_wait_ms(5u);
      continue;
    }

    log_event(ev);
    board::pulse_motor(30u);

    if (ev.type == input::EventType::Tap ||
        ev.type == input::EventType::LongPress) {
      g_last_x = ev.x;
      g_last_y = ev.y;
      g_have_touch = true;

      // Rate-limit full redraws to avoid flooding SPI on rapid taps.
      const std::uint32_t now = board::micros();
      if (static_cast<std::int32_t>(now - last_redraw_us) > 50000) {
        redraw_scene();
        last_redraw_us = now;
      }
    } else if (ev.type == input::EventType::Swipe) {
      // Flash a directional bar briefly.
      std::uint16_t colour = rgb888_to_565(200u, 200u, 40u);
      switch (ev.swipe) {
        case input::SwipeDir::Up:
          g_renderer.fill_rect(100u, 40u, 40u, 8u, colour);
          break;
        case input::SwipeDir::Down:
          g_renderer.fill_rect(100u, 160u, 40u, 8u, colour);
          break;
        case input::SwipeDir::Left:
          g_renderer.fill_rect(40u, 100u, 8u, 40u, colour);
          break;
        case input::SwipeDir::Right:
          g_renderer.fill_rect(192u, 100u, 8u, 40u, colour);
          break;
      }
      (void)g_renderer.flush();
    }
  }
}
