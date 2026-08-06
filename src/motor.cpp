#include "motor.hpp"

#include "board.hpp"
#include "nrf52832_regs.hpp"

#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
#include "FreeRTOS.h"
#include "timers.h"
#endif

namespace slate {
namespace motor {

namespace {

Seq g_seq;

#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)

TimerHandle_t g_timer = nullptr;

/**
 * Apply one Seq action.
 *
 * Order matters: arm the timer first, drive the pin second. If the timer
 * command queue is full the motor must stay off rather than run until the next
 * haptic — InfiniTime's `RunForDuration` has the same guard.
 */
void apply(const Seq::Action& a) {
  if (!a.schedule) {
    board::motor_off();
    return;
  }
  if (g_timer == nullptr) {
    board::motor_off();
    g_seq.cancel();
    return;
  }
  // pdMS_TO_TICKS can round a short pulse to 0 at 1024 Hz; a zero-period timer
  // is a configASSERT in timers.c.
  TickType_t period = pdMS_TO_TICKS(a.delay_ms);
  if (period == 0u) {
    period = 1u;
  }
  if (xTimerChangePeriod(g_timer, period, 0) != pdPASS ||
      xTimerStart(g_timer, 0) != pdPASS) {
    board::motor_off();
    g_seq.cancel();
    return;
  }
  if (a.motor_on) {
    board::motor_on();
  } else {
    board::motor_off();
  }
}

void on_expire(TimerHandle_t) { apply(g_seq.expire()); }

#endif  // SLATE_HAS_FREERTOS

void pin_init() {
  // SystemInit() already forces the pin high (off) before anything else can
  // run; repeat it here so the driver owns a known state without depending on
  // startup order.
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(board::kMotorPin)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT |
      nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
  board::motor_off();
}

}  // namespace

void init() {
  pin_init();
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  if (g_timer == nullptr) {
    // One-shot, re-armed per step (InfiniTime's shortVib). The 1-tick initial
    // period is replaced on every start.
    g_timer = xTimerCreate("vib", 1, pdFALSE, nullptr, on_expire);
  }
#endif
}

void run_for_duration(std::uint16_t ms) {
  if (ms == 0u) {
    return;
  }
  const PatternDesc d{ms, 1u, 0u};
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  stop();
  apply(g_seq.begin(d));
#else
  // Bare-metal bisect builds have no timer daemon. Blocking here is the old
  // behaviour and is acceptable only because those images have no app task.
  (void)g_seq;
  board::pulse_motor(d.on_ms);
#endif
}

void play(std::uint8_t pattern) {
  const PatternDesc d = pattern_desc(pattern);
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  stop();
  apply(g_seq.begin(d));
#else
  for (std::uint8_t i = 0u; i < d.pulses; ++i) {
    if (i != 0u) {
      board::busy_wait_ms(d.gap_ms);
    }
    board::pulse_motor(d.on_ms);
  }
#endif
}

void stop() {
  g_seq.cancel();
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  if (g_timer != nullptr) {
    (void)xTimerStop(g_timer, 0);
  }
#endif
  board::motor_off();
}

bool busy() { return g_seq.active(); }

}  // namespace motor
}  // namespace slate
