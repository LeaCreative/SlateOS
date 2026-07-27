#include "backlight.hpp"
#include "ble_conn.hpp"
#include "ble_gatt.hpp"
#include "ble_link.hpp"
#include "ble_mbuf_stats.hpp"
#include "board.hpp"
#include "button.hpp"
#include "cst816s.hpp"
#include "input_event.hpp"
#include "input_router.hpp"
#include "nrf52832_regs.hpp"
#include "renderer.hpp"
#include "rtt.hpp"
#include "sdp_diag.hpp"
#include "sdp_interpreter.hpp"
#include "sdp_opcodes.hpp"
#include "session.hpp"
#include "spi_bus.hpp"
#include "st7789.hpp"
#include "slate_uuids.hpp"
#include "twi.hpp"

#include <cstdint>

static void timer1_init() {
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_STOP) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_CLEAR) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::MODE) = nrf::timer1::MODE_TIMER;
  nrf::reg<std::uint32_t>(nrf::timer1::BITMODE) = nrf::timer1::BITMODE_32;
  nrf::reg<std::uint32_t>(nrf::timer1::PRESCALER) = 4u;
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_START) = 1u;
}

static const std::uint8_t kListWatchFace[] = {
    sdp::op::CLEAR, 0x00, 0x00, 0x00,
    sdp::op::SET_PALETTE, 0x00, 0xFF, 0xFF,
    sdp::op::SET_PALETTE, 0x01, 0xE0, 0x07,
    sdp::op::TEXT, 0x00, 120, 100, 0x01,
    sdp::align::CENTER, 5, 'S', 'l', 'a', 't', 'e',
    sdp::op::COMMIT, 0x00,
};

static Renderer g_renderer;
static sdp::Interpreter g_interp;
static ble::Link g_link;
static ble::GattServer g_gatt;
static sdp::diag::Bench g_diag;
static slate::session::Manager g_session;
static slate::InputRouter g_input;
static bool g_stale_overlay = false;

static std::uint32_t now_ms() { return board::micros() / 1000u; }

static bool link_send(std::uint8_t channel, const std::uint8_t* msg,
                      std::size_t len, void* ctx) {
  auto* link = static_cast<ble::Link*>(ctx);
  return link->send_message(channel, msg, len);
}

static slate::session::ApplyListResult apply_list(const std::uint8_t* data,
                                                  std::size_t len, void*) {
  const sdp::PushStats s = g_interp.push_list(data, len);
  if (s.status != sdp::Status::Ok) {
    return slate::session::ApplyListResult::Reject;
  }
  g_input.set_hits(g_interp.hit_rects(), g_interp.hit_count());
  return slate::session::ApplyListResult::Ok;
}

static void show_watch_face(bool stale, void*) {
  g_stale_overlay = stale;
  (void)g_interp.push_list(kListWatchFace, sizeof(kListWatchFace));
  g_input.set_hits(g_interp.hit_rects(), g_interp.hit_count());
  if (stale) {
    rtt::log(rtt::Level::Warn, "session stale overlay");
  }
}

static void apply_profile(const slate::profile::Desc& desc, void*) {
  backlight::set(desc.backlight);
  rtt::write("profile ");
  rtt::write(desc.name);
  rtt::write_line("");
  // Conn-interval renegotiation is applied by the NimBLE path when linked.
  (void)ble::interval_for(
      desc.id == slate::profile::kIdAmbient     ? ble::SessionProfile::Ambient
      : desc.id == slate::profile::kIdStreaming ? ble::SessionProfile::Streaming
                                                : ble::SessionProfile::Active);
}

static void session_log(const char* msg, void*) {
  rtt::log(rtt::Level::Info, msg);
}

static bool diag_reply(const std::uint8_t* msg, std::size_t len, void* ctx) {
  return static_cast<ble::Link*>(ctx)->reply_diag(msg, len);
}

static sdp::diag::RenderReport diag_render(const std::uint8_t* dl,
                                           std::size_t len, void* ctx) {
  auto* interp = static_cast<sdp::Interpreter*>(ctx);
  sdp::diag::RenderReport out{};
  if (interp == nullptr) {
    out.status = 1u;
    return out;
  }
  const sdp::PushStats s = interp->push_list(dl, len);
  out.parse_us = s.parse_us;
  out.render_us = s.render_us;
  out.ops = s.ops;
  out.status = static_cast<std::uint8_t>(s.status);
  return out;
}

static sdp::diag::MbufReport diag_mbuf(void*) {
  return ble::mbuf_stats().report();
}

static bool send_input_msg(const std::uint8_t* msg, std::size_t len, void* ctx) {
  return static_cast<ble::Link*>(ctx)->send_message(sdp::frame::kChanInput, msg,
                                                    len);
}

static void do_haptic(std::uint8_t pattern, void*) {
  std::uint32_t ms = 30u;
  if (pattern == sdp::haptic_pattern::LONG) ms = 80u;
  if (pattern == sdp::haptic_pattern::DOUBLE) {
    board::pulse_motor(25u);
    board::busy_wait_ms(40u);
    board::pulse_motor(25u);
    return;
  }
  board::pulse_motor(ms);
}

static void on_app_message(std::uint8_t channel, const std::uint8_t* msg,
                           std::size_t len, void*) {
  const std::uint32_t t = now_ms();
  if (channel == sdp::frame::kChanControl) {
    g_session.on_control(msg, len, t);
  } else if (channel == sdp::frame::kChanDisplay) {
    g_session.on_display(msg, len, t);
  }
}

extern "C" int main() {
  rtt::init();
  rtt::log(rtt::Level::Info, "Slate M7 — input + session");
  rtt::log(rtt::Level::Info, slate::uuid::kBaseString);
  timer1_init();

  spi::init();
  st7789::init();
  backlight::init();
  backlight::set(55u);
  twi::init();
  cst816s::init();
  button::init();
  input::init();

  g_interp.init(&g_renderer);
  (void)g_interp.push_list(kListWatchFace, sizeof(kListWatchFace));

#if defined(SLATE_BLE_DIAG) && (SLATE_BLE_DIAG == 1)
  const bool diag = true;
#else
  const bool diag = false;
#endif

  g_link.init(diag);
  g_link.set_app_handler(&on_app_message, nullptr);

  slate::session::Hooks sh;
  sh.send = &link_send;
  sh.apply_list = &apply_list;
  sh.show_watch_face = &show_watch_face;
  sh.apply_profile = &apply_profile;
  sh.log = &session_log;
  sh.ctx = &g_link;
  g_session.init(sh);
  g_session.set_diag_available(diag);

  slate::InputRouter::Hooks ih;
  ih.send_input = &send_input_msg;
  ih.haptic = &do_haptic;
  ih.ctx = &g_link;
  g_input.init(ih, &g_interp, &g_session);
  g_input.set_hits(g_interp.hit_rects(), g_interp.hit_count());

  if (diag) {
    g_diag.init(&board::micros, &diag_reply, &g_link, &diag_render, &g_interp,
                &diag_mbuf, nullptr);
    g_link.set_diag_bench(&g_diag);
    ble::mbuf_stats().reset();
  }

  ble::GattCaps caps;
  g_gatt.init(&g_link, caps);
  ble::start_stack(&g_gatt, ble::SessionProfile::Active);

  // Treat stack start as link-up for stub builds; NimBLE path should call
  // on_link_up / on_link_down from GAP events when SLATE_HAS_NIMBLE=1.
  g_session.on_link_up(now_ms());

  rtt::log(rtt::Level::Info, "M7 ready");
  std::uint32_t last_tick = now_ms();
  while (true) {
    const input::Event ev = input::poll();
    if (ev.type != input::EventType::None) {
      g_input.on_event(ev);
    }
    const std::uint32_t t = now_ms();
    if (t - last_tick >= 200u) {
      g_session.tick(t);
      last_tick = t;
    }
    board::busy_wait_ms(5u);
    (void)g_stale_overlay;
  }
}
