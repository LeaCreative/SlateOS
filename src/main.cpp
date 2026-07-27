#include "alarm_sched.hpp"
#include "backlight.hpp"
#include "battery_hw.hpp"
#include "ble_conn.hpp"
#include "ble_gatt.hpp"
#include "ble_link.hpp"
#include "ble_mbuf_stats.hpp"
#include "bma42x.hpp"
#include "board.hpp"
#include "button.hpp"
#include "cst816s.hpp"
#include "input_event.hpp"
#include "input_router.hpp"
#include "local_core.hpp"
#include "nrf52832_regs.hpp"
#include "persist.hpp"
#include "persist_nvmc.hpp"
#include "renderer.hpp"
#include "rtc_hw.hpp"
#include "rtt.hpp"
#include "sdp_diag.hpp"
#include "sdp_interpreter.hpp"
#include "sdp_opcodes.hpp"
#include "session.hpp"
#include "spi_bus.hpp"
#include "st7789.hpp"
#include "slate_uuids.hpp"
#include "twi.hpp"
#include "wall_clock.hpp"

#include <cstdint>

static void timer1_init() {
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_STOP) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_CLEAR) = 1u;
  nrf::reg<std::uint32_t>(nrf::timer1::MODE) = nrf::timer1::MODE_TIMER;
  nrf::reg<std::uint32_t>(nrf::timer1::BITMODE) = nrf::timer1::BITMODE_32;
  nrf::reg<std::uint32_t>(nrf::timer1::PRESCALER) = 4u;
  nrf::reg<std::uint32_t>(nrf::timer1::TASKS_START) = 1u;
}

static Renderer g_renderer;
static sdp::Interpreter g_interp;
static ble::Link g_link;
static ble::GattServer g_gatt;
static sdp::diag::Bench g_diag;
static slate::session::Manager g_session;
static slate::InputRouter g_input;
static slate::bma::Driver g_bma;
static slate::core::Core g_core;
static bool g_local_owns_screen = true;

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
  g_local_owns_screen = false;
  g_input.set_hits(g_interp.hit_rects(), g_interp.hit_count());
  return slate::session::ApplyListResult::Ok;
}

static void show_watch_face(bool stale, void*) {
  g_core.set_remote_stale(stale);
  g_local_owns_screen = true;
  g_core.local_state().screen = slate::local::Screen::Face;
  g_core.show_current();
}

static void apply_profile(const slate::profile::Desc& desc, void*) {
  backlight::set(desc.backlight);
  rtt::write("profile ");
  rtt::write(desc.name);
  rtt::write_line("");
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

static void core_push_list(const std::uint8_t* data, std::size_t len, void*) {
  (void)g_interp.push_list(data, len);
  g_input.set_hits(g_interp.hit_rects(), g_interp.hit_count());
}

static void core_backlight(std::uint8_t pct, void*) { backlight::set(pct); }

static bool bma_write(std::uint8_t reg, const std::uint8_t* data, std::size_t len,
                      void*) {
  std::uint8_t buf[16];
  if (len + 1u > sizeof(buf)) {
    return false;
  }
  buf[0] = reg;
  for (std::size_t i = 0u; i < len; ++i) {
    buf[i + 1u] = data[i];
  }
  return twi::write(slate::bma::kI2cAddr, buf, len + 1u) == twi::Status::Ok;
}

static bool bma_read(std::uint8_t reg, std::uint8_t* data, std::size_t len,
                     void*) {
  return twi::write_read(slate::bma::kI2cAddr, &reg, 1u, data, len) ==
         twi::Status::Ok;
}

static void bma_delay(std::uint32_t ms, void*) { board::busy_wait_ms(ms); }

static std::uint64_t clock_ticks(void*) { return slate::rtc_hw::ticks(); }

static void on_app_message(std::uint8_t channel, const std::uint8_t* msg,
                           std::size_t len, void*) {
  const std::uint32_t t = now_ms();
  if (channel == sdp::frame::kChanControl) {
    g_session.on_control(msg, len, t);
    // Optional TIME sync on CONTROL: op 0x20, unix u32 LE (host/tests / pre-CTS).
    if (len >= 5u && msg[0] == 0x20u) {
      const std::uint32_t epoch = static_cast<std::uint32_t>(msg[1]) |
                                  (static_cast<std::uint32_t>(msg[2]) << 8) |
                                  (static_cast<std::uint32_t>(msg[3]) << 16) |
                                  (static_cast<std::uint32_t>(msg[4]) << 24);
      g_core.apply_cts_time(epoch);
    }
  } else if (channel == sdp::frame::kChanDisplay) {
    g_session.on_display(msg, len, t);
  } else if (channel == sdp::frame::kChanSystem) {
    g_core.on_system_message(msg, len);
  }
}

extern "C" int main() {
  rtt::init();
  rtt::log(rtt::Level::Info, "Slate M10 — resilient local core");
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

  slate::rtc_hw::init();
  slate::persist_nvmc::init();
  slate::persist::Hooks ph;
  ph.read = &slate::persist_nvmc::read_slot;
  ph.write = &slate::persist_nvmc::write_slot;
  slate::persist::init(ph);

  slate::clock::Hooks ch;
  ch.ticks = &clock_ticks;
  slate::clock::init(ch);

  slate::battery_hw::init();

  slate::bma::Bus bus;
  bus.write_reg = &bma_write;
  bus.read_reg = &bma_read;
  bus.delay_ms = &bma_delay;
  g_bma.init(bus);
  if (g_bma.chip() == slate::bma::Chip::BMA421) {
    rtt::log(rtt::Level::Info, "BMA421");
  } else if (g_bma.chip() == slate::bma::Chip::BMA425) {
    rtt::log(rtt::Level::Info, "BMA425");
  } else {
    rtt::log(rtt::Level::Warn, "BMA unknown / missing");
  }

  g_interp.init(&g_renderer);

  slate::core::Hooks ckh;
  ckh.push_list = &core_push_list;
  ckh.haptic = &do_haptic;
  ckh.backlight = &core_backlight;
  g_core.init(ckh, &g_bma);

  {
    const auto b = g_core.budgets();
    rtt::write("budget local ");
    // Digit-only RTT: report used/budget roughly via logs.
    rtt::log(rtt::Level::Info, "local-screen-state / notif-store sized (see docs)");
    (void)b;
  }

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

  g_session.on_link_up(now_ms());
  g_core.on_link_up();

  rtt::log(rtt::Level::Info, "M10 ready");
  std::uint32_t last_tick = now_ms();
  while (true) {
    const input::Event ev = input::poll();
    if (ev.type != input::EventType::None) {
      if (g_local_owns_screen || g_session.remote_depth() == 0u) {
        if (ev.type == input::EventType::Button) {
          g_core.on_button_press();
        } else {
          g_input.on_event(ev);
        }
      } else {
        g_input.on_event(ev);
      }
    }
    const std::uint32_t t = now_ms();
    if (t - last_tick >= 200u) {
      g_session.tick(t);
      g_core.set_remote_stale(g_session.stale());
      g_core.tick(t);
      last_tick = t;
    }
    board::busy_wait_ms(5u);
  }
}
