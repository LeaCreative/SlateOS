#include "power.hpp"

#include "backlight.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "rtt.hpp"
#include "spi_bus.hpp"
#include "st7789.hpp"
#include "twi.hpp"
#include "lfs_fs.hpp"
#include "wdt.hpp"

#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
#include "FreeRTOS.h"
#include "task.h"
#endif

#include <cstdint>

namespace slate {
namespace power {

void buses_idle();
void buses_wake();

namespace {

Hooks g_hooks{};
State g_state = State::Active;
bool g_lcd_asleep = false;

#if defined(SLATE_POWER_INSTRUM) && (SLATE_POWER_INSTRUM == 1)
constexpr bool kInstrum = true;
#else
constexpr bool kInstrum = false;
#endif

const char* name_of(State s) {
  switch (s) {
    case State::Ambient: return "ambient";
    case State::Glance: return "glance";
    case State::Active: return "active";
    case State::Streaming: return "streaming";
  }
  return "?";
}

void log_transition(State from, State to) {
  if (!kInstrum) {
    return;
  }
  rtt::write("PWR t=0x");
  rtt::write_hex(board::micros());
  rtt::write(" ");
  rtt::write(name_of(from));
  rtt::write("->");
  rtt::write(name_of(to));
  rtt::write_line("");
}

void lcd_sleep_if_needed(bool want_sleep) {
  if (want_sleep && !g_lcd_asleep) {
    buses_wake();
    st7789::sleep_in();
    g_lcd_asleep = true;
    buses_idle();
  } else if (!want_sleep && g_lcd_asleep) {
    buses_wake();
    st7789::sleep_out();
    g_lcd_asleep = false;
  }
}

void apply_radio(std::uint16_t interval_units) {
  if (g_hooks.request_conn_interval) {
    (void)g_hooks.request_conn_interval(interval_units, g_hooks.ctx);
  }
}

}  // namespace

volatile bool g_rtc_wake = false;  // retained for ABI; unused with RTC1 tick

void init(const Hooks& hooks) {
  g_hooks = hooks;
  g_state = State::Active;
  g_lcd_asleep = false;
  g_rtc_wake = false;

  // RTC2 is free: FreeRTOS tick + wall clock share RTC1; NimBLE keeps RTC0.
  // Idle sleep is tickless WFE on RTC1 (port_rtc_tick.c), not a dedicated alarm.

  prefer_low_power();
  hrs_sleep();
  buses_idle();
  if (kInstrum) {
    rtt::log(rtt::Level::Info, "PWR instrum on — correlate RTT PWR lines with current trace");
  }
}

void buses_idle() {
  if (spi::is_acquired()) {
    return;
  }
  nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::DISABLE;
  nrf::reg<std::uint32_t>(nrf::twim1::ENABLE) = nrf::twim1::DISABLE;
  slate::fs::sleep_flash();
}

void buses_wake() {
  // Re-enable buses only — flash stays in DPD until xt25/fs wake it.
  nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::ENABLE_SPIM;
  nrf::reg<std::uint32_t>(nrf::twim1::ENABLE) = nrf::twim1::ENABLE_TWIM;
}

void prefer_low_power() {
  nrf::reg<std::uint32_t>(nrf::power::TASKS_LOWPWR) = 1u;
}

void disable_debug_assist() {
  // Cannot fully power-gate SWD from software after a debug session without
  // APPROTECT / power cycle — see docs/power.md. Prefer LOWPOWER mode.
  prefer_low_power();
  if (kInstrum) {
    rtt::log(rtt::Level::Warn,
             "PWR: soft debug-assist off — power-cycle after SWD disconnect for Gate C");
  }
}

void hrs_sleep() {
  // HRS3300 PDRIVER (0x0C) = 0 → sleep. Address 0x44 typical on PineTime.
  constexpr std::uint8_t kAddr = 0x44u;
  constexpr std::uint8_t kPdriver = 0x0Cu;
  std::uint8_t pkt[2] = {kPdriver, 0x00u};
  buses_wake();
  (void)twi::write(kAddr, pkt, 2u, 3000u);
  buses_idle();
}

int sample_battery_adc() {
  alignas(4) static std::int16_t result = 0;
  buses_wake();

  nrf::reg<std::uint32_t>(nrf::saadc::ENABLE) = 0u;
  nrf::reg<std::uint32_t>(nrf::saadc::RESOLUTION) = nrf::saadc::RES_10BIT;
  nrf::reg<std::uint32_t>(nrf::saadc::OVERSAMPLE) = 0u;
  nrf::reg<std::uint32_t>(nrf::saadc::SAMPLERATE) = 0u;
  nrf::reg<std::uint32_t>(nrf::saadc::CH0_PSELP) = nrf::saadc::PSELP_AIN7;
  nrf::reg<std::uint32_t>(nrf::saadc::CH0_PSELN) = 0u;
  // Gain 1/4 + internal 0.6 V ref (InfiniTime's BatteryController settings),
  // TACQ 10us, single-ended. The GAIN field previously held 5, which is gain
  // *1* — full scale 0.6 V against ~2.0 V on the divided pin, so every sample
  // railed and the battery always read 0 % (N-12).
  nrf::reg<std::uint32_t>(nrf::saadc::CH0_CONFIG) =
      (0u << 0) | (nrf::saadc::GAIN_1_4 << 8) |
      (nrf::saadc::REFSEL_INTERNAL << 12) | (2u << 16) | (0u << 20) |
      (0u << 24);
  nrf::reg<std::uint32_t>(nrf::saadc::RESULT_PTR) =
      reinterpret_cast<std::uint32_t>(&result);
  nrf::reg<std::uint32_t>(nrf::saadc::RESULT_MAXCNT) = 1u;

  nrf::reg<std::uint32_t>(nrf::saadc::ENABLE) = nrf::saadc::ENABLE_ON;
  nrf::reg<std::uint32_t>(nrf::saadc::EVENTS_STARTED) = 0u;
  nrf::reg<std::uint32_t>(nrf::saadc::EVENTS_END) = 0u;
  nrf::reg<std::uint32_t>(nrf::saadc::EVENTS_STOPPED) = 0u;

  nrf::reg<std::uint32_t>(nrf::saadc::TASKS_START) = 1u;
  std::uint32_t t0 = board::micros();
  while (nrf::reg<std::uint32_t>(nrf::saadc::EVENTS_STARTED) == 0u) {
    if (board::micros() - t0 > 5000u) {
      break;
    }
  }
  nrf::reg<std::uint32_t>(nrf::saadc::TASKS_SAMPLE) = 1u;
  t0 = board::micros();
  while (nrf::reg<std::uint32_t>(nrf::saadc::EVENTS_END) == 0u) {
    if (board::micros() - t0 > 5000u) {
      break;
    }
  }

  // STOPPED (not merely ENABLE=0) — required for µA-class residual.
  nrf::reg<std::uint32_t>(nrf::saadc::TASKS_STOP) = 1u;
  t0 = board::micros();
  while (nrf::reg<std::uint32_t>(nrf::saadc::EVENTS_STOPPED) == 0u) {
    if (board::micros() - t0 > 5000u) {
      break;
    }
  }
  nrf::reg<std::uint32_t>(nrf::saadc::ENABLE) = 0u;
  buses_idle();
  return static_cast<int>(result);
}

State current() { return g_state; }

void enter(State s) {
  if (s == g_state) {
    return;
  }
  const State from = g_state;
  g_state = s;
  log_transition(from, s);

  switch (s) {
    case State::Ambient:
      backlight::off();
      lcd_sleep_if_needed(true);
      buses_idle();
      apply_radio(400u);  // 500 ms
      break;
    case State::Glance:
      lcd_sleep_if_needed(false);
      backlight::on_low();
      buses_idle();
      // Interval unchanged per §8 glance.
      break;
    case State::Active:
      lcd_sleep_if_needed(false);
      backlight::on_mid();
      apply_radio(24u);  // 30 ms
      break;
    case State::Streaming:
      lcd_sleep_if_needed(false);
      backlight::on_high();
      apply_radio(12u);  // 15 ms
      break;
  }
}

void apply_profile(const profile::Desc& desc) {
  State s = State::Active;
  if (desc.id == profile::kIdAmbient) {
    s = State::Ambient;
  } else if (desc.id == profile::kIdStreaming) {
    s = State::Streaming;
  } else {
    s = State::Active;
  }
  // Honour profile backlight for non-ambient; ambient forces off.
  enter(s);
  if (s != State::Ambient) {
    backlight::set(desc.backlight);
  }
  apply_radio(desc.interval_units);
}

void sleep_ms(std::uint32_t max_ms) {
  if (max_ms == 0u) {
    return;
  }
  if (max_ms > 1000u) {
    max_ms = 1000u;
  }
  buses_idle();
  prefer_low_power();
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  // Tickless idle on RTC1 handles the real sleep; this is just a delay.
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    vTaskDelay(pdMS_TO_TICKS(max_ms));
    return;
  }
#endif
  // Bare-metal / pre-scheduler: TIMER1 busy-wait with WDT pets (no RTC2).
  board::busy_wait_ms(max_ms);
}

}  // namespace power
}  // namespace slate
