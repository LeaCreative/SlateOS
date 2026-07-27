#include "backlight.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "renderer.hpp"
#include "rtt.hpp"
#include "sdp_interpreter.hpp"
#include "sdp_opcodes.hpp"
#include "spi_bus.hpp"
#include "st7789.hpp"

#include <cstdint>

static void timer1_init() {
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_STOP) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_CLEAR) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::MODE) = nrf::timer1::MODE_TIMER;
  nrf::reg<std::uint32_t>(nrf::timer1::BITMODE) = nrf::timer1::BITMODE_32;
  nrf::reg<std::uint32_t>(nrf::timer1::PRESCALER) = 4u;
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_START) = 1u;
}

static void log_stats(const char* name, const sdp::PushStats& s) {
  rtt::write(name);
  rtt::write(" status=0x");
  rtt::write_hex(static_cast<std::uint32_t>(s.status));
  rtt::write(" ops=0x");
  rtt::write_hex(s.ops);
  rtt::write(" bytes=0x");
  rtt::write_hex(static_cast<std::uint32_t>(s.bytes));
  rtt::write(" parse_us=0x");
  rtt::write_hex(s.parse_us);
  rtt::write(" render_us=0x");
  rtt::write_hex(s.render_us);
  rtt::write_line("");
}

// Helpers to pack demo lists at compile time via byte arrays.

// Clock face: dark clear, palette, time digits, progress arc, commit.
static const std::uint8_t kListClock[] = {
    sdp::op::CLEAR, 0x00, 0x00, 0x00,           // black
    sdp::op::SET_PALETTE, 0x00, 0xFF, 0xFF,     // pal0 = white
    sdp::op::SET_PALETTE, 0x01, 0xE0, 0x07,     // pal1 = green
    // TEXT "1234" at centre-ish
    sdp::op::TEXT, 0x00, 88, 100, 0x01,         // font0, x,y, pal0
    sdp::align::CENTER, 4, '1', '2', ':', '4',
    // PROGRESS_ARC
    sdp::op::PROGRESS_ARC, 120, 120, 50, 40,
    0x02,                                       // fg pal1
    0x00, 0x08, 0x08,                           // bg literal dark
    3,                                          // width
    sdp::op::COMMIT, 0x00,
};

// Notification: header bar, body text box, action button elem, commit.
static const std::uint8_t kListNotification[] = {
    sdp::op::CLEAR, 0x00, 0x10, 0x00,           // dark blue-ish
    sdp::op::SET_PALETTE, 0x00, 0xFF, 0xFF,
    sdp::op::SET_PALETTE, 0x01, 0x00, 0xF8,     // red
    sdp::op::RECT, 0, 0, 240, 36, 0x00, 0x08, 0x08, 0x00,  // header fill dark
    sdp::op::TEXT, 0x00, 8, 12, 0x01, sdp::align::LEFT, 3, 'M', 's', 'g',
    sdp::op::TEXT_BOX, 0x00, 8, 50, 220, 80, 0x01, sdp::align::LEFT,
    sdp::text_box_flags::WRAP, 5, 'H', 'e', 'l', 'l', 'o',
    sdp::op::BEGIN_ELEM,
    0x01, 0x00,                                 // id=1 LE
    20, 180, 200, 40,
    sdp::elem_flags::HAPTIC,
    sdp::op::RECT, 20, 180, 200, 40, 0x02, 0x00,  // pal1 fill
    sdp::op::TEXT, 0x00, 100, 194, 0x01, sdp::align::CENTER, 2, 'O', 'K',
    sdp::op::END_ELEM,
    sdp::op::COMMIT, 0x00,
};

// Navigation: map stub polyline, instruction text, scroll region, commit.
static const std::uint8_t kListNavigation[] = {
    sdp::op::CLEAR, 0x00, 0x00, 0x00,
    sdp::op::SET_PALETTE, 0x00, 0xE0, 0x07,     // green
    sdp::op::SET_PALETTE, 0x01, 0xFF, 0xFF,
    // Header in screen coordinates (before scroll region).
    sdp::op::TEXT, 0x00, 20, 10, 0x02, sdp::align::LEFT, 4, 'T', 'u', 'r', 'n',
    sdp::op::SCROLL_REGION, 40, 160,
    0x2C, 0x01,                                 // content_h = 300 LE
    // Content-local Y inside the scroll viewport.
    sdp::op::POLYLINE, 4, 0x01, 2,
    20, 40, 80, 80, 140, 50, 200, 110,
    sdp::op::CLIP_CLEAR,                        // leave scroll content mode
    // Soft button in screen coordinates.
    sdp::op::BEGIN_ELEM,
    0x02, 0x00,
    10, 200, 100, 30, 0x00,
    sdp::op::RECT, 10, 200, 100, 30, 0x00, 0x18, 0x18, 0x00,
    sdp::op::END_ELEM,
    sdp::op::COMMIT, 0x00,
};

static Renderer g_renderer;
static sdp::Interpreter g_interp;

extern "C" int main() {
  rtt::init();
  rtt::log(rtt::Level::Info, "Slate M3 boot — SDP display-list interpreter");
  timer1_init();

  for (int i = 0; i < 2; ++i) {
    board::pulse_motor(50u);
    board::busy_wait_ms(80u);
  }

  spi::init();
  st7789::init();
  backlight::init();
  backlight::set(55u);

  g_interp.init(&g_renderer);

  {
    const sdp::PushStats s =
        g_interp.push_list(kListClock, sizeof(kListClock));
    log_stats("clock", s);
    board::busy_wait_ms(800u);
  }
  {
    const sdp::PushStats s =
        g_interp.push_list(kListNotification, sizeof(kListNotification));
    log_stats("notification", s);
    board::busy_wait_ms(800u);
  }
  {
    const sdp::PushStats s =
        g_interp.push_list(kListNavigation, sizeof(kListNavigation));
    log_stats("navigation", s);
  }

  rtt::log(rtt::Level::Info, "M3 demo complete — heartbeat");
  while (true) {
    board::busy_wait_ms(1000u);
    rtt::log(rtt::Level::Debug, "heartbeat");
  }
}
