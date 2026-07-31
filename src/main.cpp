#include "alarm_sched.hpp"
#include "asset_xfer.hpp"
#include "backlight.hpp"
#include "battery.hpp"
#include "battery_hw.hpp"
#include "ble_conn.hpp"
#include "ble_gatt.hpp"
#include "ble_link.hpp"
#include "ble_mbuf_stats.hpp"
#include "bma42x.hpp"
#include "board.hpp"
#include "boot_diag.hpp"
#include "boot_util.hpp"
#include "button.hpp"
#include "cst816s.hpp"
#include "input_event.hpp"
#include "input_router.hpp"
#include "lfs_fs.hpp"
#include "local_core.hpp"
#include "nrf52832_regs.hpp"
#include "ota_slot.hpp"
#include "ota_xfer.hpp"
#include "persist.hpp"
#include "persist_nvmc.hpp"
#include "power.hpp"
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
#include "mono_time.hpp"
#include "wall_clock.hpp"
#include "wdt.hpp"
#include "xt25.hpp"
#include "freertos_smoke.hpp"

#include <cstdint>

#if defined(SLATE_HAS_NIMBLE) && (SLATE_HAS_NIMBLE == 1)
#include "FreeRTOS.h"
#include "task.h"
#endif

static Renderer g_renderer;
static sdp::Interpreter g_interp;
static ble::Link g_link;
static ble::GattServer g_gatt;
static sdp::diag::Bench g_diag;
static slate::session::Manager g_session;
static slate::InputRouter g_input;
static slate::bma::Driver g_bma;
static slate::core::Core g_core;
static slate::asset::Receiver g_asset;
static slate::ota::Receiver g_ota;
static bool g_local_owns_screen = true;

static constexpr char kStagingPath[] = "/assets/.staging";

static bool asset_write_staging(std::uint32_t offset, const std::uint8_t* data,
                                std::size_t len, void*) {
  return slate::fs::write_file_at(kStagingPath, offset, data, len);
}

static bool asset_commit_staging(const char* name, std::uint32_t total,
                                 std::uint32_t hash, void*) {
  // Read staging, verify FNV of bytes[12..), write to /assets/<name>.
  if (name == nullptr || total < 20u) {
    return false;
  }
  // Stream hash without holding whole file in RAM.
  std::uint32_t h = 2166136261u;
  std::uint8_t buf[256];
  std::uint32_t off = 12u;
  while (off < total) {
    const std::size_t n =
        (total - off > sizeof(buf)) ? sizeof(buf) : (total - off);
    if (slate::fs::read_file(kStagingPath, off, buf, n) != n) {
      return false;
    }
    for (std::size_t i = 0u; i < n; ++i) {
      h ^= buf[i];
      h *= 16777619u;
    }
    off += static_cast<std::uint32_t>(n);
  }
  if (h != hash) {
    return false;
  }
  // Copy staging → final path in chunks.
  char path[48] = "/assets/";
  std::size_t plen = 8u;
  for (std::size_t i = 0u; name[i] != '\0' && plen + 1u < sizeof(path); ++i) {
    path[plen++] = name[i];
  }
  path[plen] = '\0';
  (void)slate::fs::remove_file(path);
  off = 0u;
  while (off < total) {
    const std::size_t n =
        (total - off > sizeof(buf)) ? sizeof(buf) : (total - off);
    if (slate::fs::read_file(kStagingPath, off, buf, n) != n) {
      return false;
    }
    if (!slate::fs::write_file_at(path, off, buf, n)) {
      return false;
    }
    off += static_cast<std::uint32_t>(n);
  }
  (void)slate::fs::remove_file(kStagingPath);
  return true;
}

static bool asset_abort_staging(void*) {
  return slate::fs::remove_file(kStagingPath);
}

static bool asset_send(const std::uint8_t* msg, std::size_t len, void* ctx) {
  return static_cast<ble::Link*>(ctx)->send_message(sdp::frame::kChanAsset, msg,
                                                    len);
}

static bool ota_erase(void*) { return slate::ota_slot::erase_all(); }
static bool ota_write(std::uint32_t off, const std::uint8_t* d, std::size_t n,
                      void*) {
  return slate::ota_slot::write(off, d, n);
}
static std::uint8_t ota_batt(void*) { return slate::battery::percent(); }
static bool ota_chg(void*) { return slate::battery::charging(); }
static bool ota_commit(std::uint32_t, const std::uint8_t*, void*) {
  // Mark pending for InfiniTime MCUBoot, then reboot to swap.
  if (!slate::ota_slot::write_infinitime_pending_magic()) {
    return false;
  }
  slate::ota_slot::request_reboot();
  return true;
}
static bool ota_send(const std::uint8_t* msg, std::size_t len, void* ctx) {
  return static_cast<ble::Link*>(ctx)->send_message(sdp::frame::kChanOta, msg,
                                                    len);
}

static std::uint32_t now_ms();

static void on_ble_session_up(void*) {
  g_session.on_link_up(now_ms());
  g_core.on_link_up();
}

static void on_ble_session_down(void*) {
  g_session.on_link_down(now_ms());
  g_core.on_link_down();
}

static std::uint32_t now_ms() { return slate::time::mono_ms(); }

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
  slate::power::apply_profile(desc);
  rtt::write("profile ");
  rtt::write(desc.name);
  rtt::write_line("");
}

static bool request_interval(std::uint16_t units, void*) {
  return ble::request_conn_interval(units);
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

static void send_confirm_status() {
  std::uint8_t buf[6];
  buf[0] = sdp::control_op::CONFIRM_STATUS;
  const bool needs = slate::boot::needs_confirm();
  buf[1] = needs ? 1u : 0u;
  const std::uint32_t rem =
      slate::boot::dwell_remaining_ms(ble::central_connected(), now_ms());
  buf[2] = static_cast<std::uint8_t>(rem & 0xFFu);
  buf[3] = static_cast<std::uint8_t>((rem >> 8) & 0xFFu);
  buf[4] = static_cast<std::uint8_t>((rem >> 16) & 0xFFu);
  buf[5] = static_cast<std::uint8_t>((rem >> 24) & 0xFFu);
  (void)g_link.send_message(sdp::frame::kChanControl, buf, sizeof(buf));
}

static void on_app_message(std::uint8_t channel, const std::uint8_t* msg,
                           std::size_t len, void*) {
  const std::uint32_t t = now_ms();
  if (channel == sdp::frame::kChanControl) {
    g_session.on_control(msg, len, t);
    // Optional TIME sync on CONTROL (control_op::TIME_SYNC + unix u32 LE).
    if (len >= 5u && msg[0] == sdp::control_op::TIME_SYNC) {
      const std::uint32_t epoch = static_cast<std::uint32_t>(msg[1]) |
                                  (static_cast<std::uint32_t>(msg[2]) << 8) |
                                  (static_cast<std::uint32_t>(msg[3]) << 16) |
                                  (static_cast<std::uint32_t>(msg[4]) << 24);
      g_core.apply_cts_time(epoch);
    }
    // Trial/confirm status query (0xE0 CONTROL extension; old FW ignores).
    if (len >= 1u && msg[0] == sdp::control_op::CONFIRM_STATUS_REQUEST) {
      send_confirm_status();
    }
  } else if (channel == sdp::frame::kChanDisplay) {
    g_session.on_display(msg, len, t);
  } else if (channel == sdp::frame::kChanSystem) {
    g_core.on_system_message(msg, len);
  } else if (channel == sdp::frame::kChanAsset) {
    // Yield while remote UI is actively receiving display lists.
    g_asset.set_yield_busy(g_session.remote_depth() > 0u &&
                           !g_local_owns_screen);
    g_asset.on_message(msg, len);
  } else if (channel == sdp::frame::kChanOta) {
    g_ota.on_message(msg, len);
  }
}

// Liveness beat: app_loop increments every iteration; tick hook only measures
// stalls into g_max_stall_ms for diag_stall_ms. It must NOT pet the WDT —
// InfiniTime feeds the dog from SystemTask only, so a wedged main task reboots.
volatile std::uint32_t g_app_beat = 0u;
volatile std::uint32_t g_max_stall_ms = 0u;

static void app_loop() {
  rtt::log(rtt::Level::Info, "M15 ready (await BLE for IMAGE_OK confirm)");
  bool trial = g_core.local_state().trial_image != 0u;
  std::uint32_t last_tick = now_ms();
  std::uint32_t last_batt = now_ms();
  bool smoke_logged = false;
  std::uint32_t iters = 0u;
  std::uint32_t paints = 0u;
  while (true) {
    ++iters;
    ++g_app_beat;
    // InfiniTime SystemTask cadence: pet only from the app task (~20 ms / ~200 ms
    // ambient). Worst-case stall inside one iteration must stay << bootloader
    // WDT (~7 s). Bound long flash work with pets in ota_slot::erase_all /
    // xt25::wait_ready — do not rely on tick/idle hooks (they deliberately do
    // not pet, so a wedged app starves the dog like InfiniTime).
    slate::wdt::pet_service();

    // N-1 / I-10 stage 1: link→app handoff. Drain GATT-reassembled SDP here so
    // session + interpreter + SPI never run on the NimBLE host task. CREDIT is
    // emitted from session::on_display after this deferred apply.
    while (g_link.drain_app_messages()) {
    }
    if (!smoke_logged && freertos_smoke::finished()) {
      smoke_logged = true;
      if (freertos_smoke::result() == freertos_smoke::Result::Pass) {
        rtt::log(rtt::Level::Info, "M5a smoke PASS (STATUS[11]=1)");
      } else {
        rtt::log(rtt::Level::Error, "M5a smoke FAIL (STATUS[11]=2)");
      }
    }
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
    if (trial && slate::boot::tick_confirm(ble::central_connected(), t)) {
      trial = false;
      g_core.local_state().trial_image = 0u;
      g_core.show_current();
      rtt::log(rtt::Level::Info, "boot: image confirmed (link held)");
      send_confirm_status();
    }
    if (t - last_tick >= 200u) {
      g_session.tick(t);
      g_core.set_remote_stale(g_session.stale());
      g_core.tick(t);
      last_tick = t;
    }
#if !defined(SLATE_DIAG_OVERLAY) || (SLATE_DIAG_OVERLAY == 1)
    // Repaint every 16 loop iterations (~0.3 s with the 20 ms delay). A 2048
    // threshold needed ~40 s for the first update and the bootloader WDT kills
    // the watch at ~7 s, so paints stayed at 0 even while the loop was alive.
    if ((iters & 0xFu) == 0u) {
      auto& st = g_core.local_state();
      st.diag_uptime_s = t / 1000u;
      st.diag_paints = ++paints;
      st.diag_button = board::button_raw() ? 1u : 0u;
      st.diag_stall_ms = g_max_stall_ms;
      ble::bringup_snapshot(&st.diag_ble_state, &st.diag_ble_rc);
      g_core.show_current();
    }
#endif
    if (t - last_batt >= 10000u) {
      // ADC refresh lives in Core::poll_battery (charge edge + 10 s). BAS only.
      if (slate::battery::percent_valid()) {
        ble::update_battery_level(slate::battery::percent());
      }
      last_batt = t;
    }
    const bool ambient =
        g_local_owns_screen && g_session.remote_depth() == 0u &&
        g_session.profile_id() == slate::profile::kIdAmbient;
    if (ambient && slate::power::current() != slate::power::State::Ambient) {
      slate::power::enter(slate::power::State::Ambient);
    }
#if defined(SLATE_HAS_NIMBLE) && (SLATE_HAS_NIMBLE == 1)
    // Wake from Link::set_app_wake short-circuits this wait when a message lands.
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ambient ? 200u : 20u));
#else
    slate::power::sleep_ms(ambient ? 200u : 20u);
#endif
  }
}

#if defined(SLATE_HAS_NIMBLE) && (SLATE_HAS_NIMBLE == 1)
static TaskHandle_t g_app_task = nullptr;

static void app_wake_from_link(void*) {
  if (g_app_task != nullptr) {
    (void)xTaskNotifyGive(g_app_task);
  }
}

static void app_task(void*) { app_loop(); }

// Idle must not pet — a blocked app task would leave idle running and keep the
// bootloader WDT fed forever (unlike InfiniTime SystemTask-only reload).
extern "C" void vApplicationIdleHook(void) {}

namespace {
std::uint32_t g_beat_seen = 0u;
std::uint32_t g_beat_stall_ticks = 0u;
}  // namespace

extern "C" void vApplicationTickHook(void) {
  // Telemetry only. WDT pets live in app_loop (and POST_SLEEP for future
  // tickless). Button-hold withhold remains inside wdt::pet() for those paths.
  if (g_app_beat != g_beat_seen) {
    g_beat_seen = g_app_beat;
    g_beat_stall_ticks = 0u;
    return;
  }
  ++g_beat_stall_ticks;
  const std::uint32_t ms = g_beat_stall_ticks * 1000u / configTICK_RATE_HZ;
  if (ms > g_max_stall_ms) {
    g_max_stall_ms = ms;
  }
}

// Fatal hooks paint a diagnostic colour (see boot_diag.hpp) before releasing the
// WDT, so a sealed watch reports the cause instead of silently reverting.
extern "C" void vApplicationMallocFailedHook(void) {
  rtt::log(rtt::Level::Error, "FreeRTOS malloc failed");
  boot_diag::fatal_paint_and_hang(boot_diag::kRed);
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char*) {
  rtt::log(rtt::Level::Error, "FreeRTOS stack overflow");
  boot_diag::fatal_paint_and_hang(boot_diag::kOrange);
}

extern "C" void vAssertCalled(const char* file, int line) {
  // Cyan plus FNV-1a(basename) and line, so a sealed watch names the assert site.
  boot_diag::assert_paint_and_hang_at(file, line);
}
#endif

extern "C" int main() {
  // Before anything else can trigger a reset of its own: RESETREAS accumulates
  // until cleared, so latching it first is the only way to attribute the reset
  // that actually just happened.
  board::capture_reset_reason();
  // Before any WDT pet: InfiniTime leaves enable high; pet() withholds while held.
  board::button_hw_init();
  rtt::init();
  slate::wdt::pet();
  rtt::log(rtt::Level::Info, "Slate M15 — OTA / MCUBoot");
  rtt::log(rtt::Level::Info, slate::uuid::kBaseString);
  slate::power::disable_debug_assist();

  spi::init();
  st7789::init();
  backlight::init();
  backlight::set(55u);
  twi::init();
  cst816s::init();
  button::init();
  input::init();

  slate::power::Hooks phooks;
  phooks.request_conn_interval = &request_interval;
  slate::power::init(phooks);

  slate::xt25::init();
  slate::wdt::pet();
  if (slate::fs::mount()) {
    rtt::log(rtt::Level::Info, "LittleFS mounted");
    slate::fs::sleep_flash();
  } else {
    rtt::log(rtt::Level::Warn, "LittleFS mount failed");
  }

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
  ckh.sample_battery = [](void*) { slate::battery_hw::sample_now(); };
  g_core.init(ckh, &g_bma);
  // init() memsets the state block and paints from it, so these have to be set
  // afterwards — and then repainted explicitly. Without the repaint the amber
  // trial marker stayed invisible on any watch that reset before the app task
  // first ran, which is exactly the case being debugged.
  g_core.local_state().trial_image = slate::boot::needs_confirm() ? 1u : 0u;
  g_core.local_state().diag_reset_reason = board::reset_reason();
  g_core.show_current();

  slate::asset::Hooks ah;
  ah.write_staging = &asset_write_staging;
  ah.commit_staging = &asset_commit_staging;
  ah.abort_staging = &asset_abort_staging;
  ah.send = &asset_send;
  ah.ctx = &g_link;
  g_asset.init(ah);

  slate::ota_slot::init();
  slate::ota::Hooks oh;
  oh.erase_slot = &ota_erase;
  oh.write_slot = &ota_write;
  oh.battery_percent = &ota_batt;
  oh.charging = &ota_chg;
  oh.commit_ok = &ota_commit;
  oh.send = &ota_send;
  oh.ctx = &g_link;
  g_ota.init(oh);

  ble::set_session_up_hook(&on_ble_session_up, nullptr);
  ble::set_session_down_hook(&on_ble_session_down, nullptr);

  {
    const auto b = g_core.budgets();
    rtt::write("budget local ");
    rtt::log(rtt::Level::Info, "local-screen-state / notif-store sized (see docs)");
    (void)b;
  }

#if defined(SLATE_BLE_DIAG) && (SLATE_BLE_DIAG == 1)
  const bool diag = true;
#else
  const bool diag = false;
#endif

  g_link.init(diag);
  slate::wdt::pet();
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

#if defined(SLATE_HAS_NIMBLE) && (SLATE_HAS_NIMBLE == 1)
  // App task owns UI/WDT; NimBLE creates LL+host tasks inside start_stack.
  // M5a smoke is Debug-only — Release must keep peak heap for ll+ble+app.
#if defined(SLATE_BLE_DIAG) && (SLATE_BLE_DIAG == 1)
  freertos_smoke::create_tasks();
#endif
  // 768 words (3 KiB): leaves FreeRTOS heap for NimBLE event queues. 1024-word
  // app + 14 KiB heap exhausted xQueueCreate → NULL → queue.c configASSERT.
  if (xTaskCreate(app_task, "app", 768, nullptr, tskIDLE_PRIORITY + 2,
                  &g_app_task) != pdPASS) {
    // Heap exhausted before the scheduler even starts — same cause as a
    // malloc-failed hook, so use the same colour. Never pet forever here.
    rtt::log(rtt::Level::Error, "app task create failed");
    boot_diag::fatal_paint_and_hang(boot_diag::kRed);
  }
  g_link.set_app_wake(&app_wake_from_link, nullptr);
  ble::start_stack(&g_gatt, ble::SessionProfile::Active);
  slate::wdt::pet();
  vTaskStartScheduler();
  rtt::log(rtt::Level::Error, "scheduler returned");
  boot_diag::fatal_paint_and_hang(boot_diag::kRed);
#else
  ble::start_stack(&g_gatt, ble::SessionProfile::Active);
  slate::wdt::pet();
  app_loop();
#endif
  return 0;
}
