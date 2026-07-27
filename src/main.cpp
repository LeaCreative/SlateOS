#include "backlight.hpp"
#include "ble_gatt.hpp"
#include "ble_link.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "renderer.hpp"
#include "rtt.hpp"
#include "sdp_interpreter.hpp"
#include "sdp_opcodes.hpp"
#include "spi_bus.hpp"
#include "st7789.hpp"
#include "slate_uuids.hpp"

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

static const std::uint8_t kListClock[] = {
    sdp::op::CLEAR, 0x00, 0x00, 0x00,
    sdp::op::SET_PALETTE, 0x00, 0xFF, 0xFF,
    sdp::op::SET_PALETTE, 0x01, 0xE0, 0x07,
    sdp::op::TEXT, 0x00, 88, 100, 0x01,
    sdp::align::CENTER, 4, '1', '2', ':', '4',
    sdp::op::PROGRESS_ARC, 120, 120, 50, 40,
    0x02,
    0x00, 0x08, 0x08,
    3,
    sdp::op::COMMIT, 0x00,
};

static Renderer g_renderer;
static sdp::Interpreter g_interp;
static ble::Link g_link;
static ble::GattServer g_gatt;

extern "C" int main() {
  rtt::init();
  rtt::log(rtt::Level::Info, "Slate M5 boot — BLE transport + SDP framing");
  rtt::log(rtt::Level::Info, slate::uuid::kBaseString);
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
    const sdp::PushStats s = g_interp.push_list(kListClock, sizeof(kListClock));
    log_stats("clock", s);
  }

#if defined(SLATE_BLE_DIAG) && (SLATE_BLE_DIAG == 1)
  const bool diag = true;
#else
  const bool diag = false;
#endif

  g_link.init(diag);
  ble::GattCaps caps;
  g_gatt.init(&g_link, caps);
  ble::start_stack(&g_gatt, ble::SessionProfile::Active);

  rtt::log(rtt::Level::Info, "M5 transport ready — heartbeat");
  while (true) {
    board::busy_wait_ms(1000u);
    rtt::log(rtt::Level::Debug, "heartbeat");
  }
}
