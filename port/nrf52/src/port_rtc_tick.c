/*
 * FreeRTOS tick from nRF52832 RTC1 (InfiniTime / Nordic SDK pattern).
 *
 * SysTick cannot drive tickless idle on nRF52: the core clock is gated in
 * System ON sleep, so SysTick neither counts nor wakes the CPU. RTC1 runs from
 * the LFXO, so it keeps counting through WFE and can wake us via COMPARE0.
 *
 * RTC0 stays with NimBLE os_cputime. RTC2 is unused (power::sleep_ms retired).
 * Wall clock shares this RTC1 COUNTER (1024 Hz) via rtc_hw.
 *
 * IMPORTANT: do not catch up FreeRTOS ticks from (COUNTER - xTaskGetTickCount())
 * without a hard cap. After tickless vTaskStepTick the freeRTOS tick can sit
 * slightly ahead of COUNTER; the 24-bit subtract then underflows to ~16M and
 * the ISR spins until the bootloader WDT reverts (~7 s). That was the first
 * RTC-tick regression, and the guards below exist to make catch-up safe:
 * a diff in the top half of the range means "tick is ahead", never a huge
 * catch-up, and each ISR steps at most kMaxCatchUp ticks.
 *
 * Catch-up itself is REQUIRED (N-10). This handler runs at the kernel (lowest)
 * interrupt priority, below NimBLE's RADIO ISR (priority 5) and level with its
 * RTC0/TIMER0 ISRs, so once the radio is live the handler is regularly held off
 * for longer than the 0.98 ms tick period. RTC TICK events coalesce — the
 * peripheral just re-sets one event flag — so "one increment per event" loses
 * time permanently and the FreeRTOS clock free-runs slow. Everything scheduled
 * in ticks then stretches in real time, including app_loop's 20 ms wait and
 * therefore the WDT pet cadence, until the bootloader's real-time 7 s dog
 * bites. InfiniTime's stock port (src/FreeRTOS/port_cmsis_systick.c,
 * FREERTOS_USE_RTC) corrects from COUNTER for exactly this reason; under the
 * mirror rule that is the default.
 *
 * See also the priority note in vPortSetupTimerInterrupt (N-11): until that
 * was fixed this handler ran at priority 0, so it was never held off at all —
 * and could pre-empt task critical sections instead.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "nrf.h"

#include <stdint.h>

#if !defined(SLATE_FREERTOS_RTC_TICK)
#error "port_rtc_tick.c requires -DSLATE_FREERTOS_RTC_TICK"
#endif

/* wall_clock overflow counter — implemented in rtc_hw.cpp */
void slate_rtc1_on_overflow(void);

/* Ticks recovered by catch-up since boot, i.e. TICK events that coalesced
 * while this handler was held off. Surfaced in the sealed diag overlay: 0
 * means the tick never fell behind, a climbing value quantifies the loss
 * that used to go silently missing (N-10). */
static volatile uint32_t s_catchup_ticks = 0u;

uint32_t slate_rtc_tick_catchup(void) { return s_catchup_ticks; }

/* Bound the worst-case time spent in one ISR. Anything beyond this is
 * recovered on following TICK events (one every ~0.98 ms), so a large gap
 * still closes quickly without a long interrupt. */
#define kMaxCatchUp 128u

#define portNRF_RTC_REG        NRF_RTC1
#define portNRF_RTC_IRQn       RTC1_IRQn
#define portNRF_RTC_MAXTICKS   ((1u << 24) - 1u)
#define portNRF_RTC_PRESCALER \
  ((uint32_t)((configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ) - 1u))

/* Errata 87: clear FPU exceptions before sleep or a sticky FPU IRQ wakes us. */
#define FPU_EXCEPTION_MASK 0x0000009Fu

void vPortSetupTimerInterrupt(void) {
  portNRF_RTC_REG->TASKS_STOP = 1u;
  portNRF_RTC_REG->TASKS_CLEAR = 1u;
  portNRF_RTC_REG->PRESCALER = portNRF_RTC_PRESCALER; /* 32768/1024 - 1 = 31 */

  portNRF_RTC_REG->EVTENSET =
      RTC_EVTENSET_TICK_Msk | RTC_EVTENSET_OVRFLW_Msk;
  portNRF_RTC_REG->INTENSET =
      RTC_INTENSET_TICK_Msk | RTC_INTENSET_OVRFLW_Msk;
  portNRF_RTC_REG->EVENTS_TICK = 0u;
  portNRF_RTC_REG->EVENTS_OVRFLW = 0u;
  portNRF_RTC_REG->EVENTS_COMPARE[0] = 0u;

  /* Raw priority number, NOT configKERNEL_INTERRUPT_PRIORITY (N-11).
   * CMSIS NVIC_SetPriority shifts internally — (p << (8 - __NVIC_PRIO_BITS)) —
   * while configKERNEL_INTERRUPT_PRIORITY is the ALREADY-shifted register form
   * that FreeRTOS pokes straight into SHPR. Passing it here shifted twice:
   * (7 << 5) << 5 & 0xFF == 0, so the tick ran at priority 0, above the NimBLE
   * radio (5) and above configMAX_SYSCALL_INTERRUPT_PRIORITY (3) — meaning
   * portENTER_CRITICAL's BASEPRI could not mask it and xTaskIncrementTick
   * could mutate kernel lists inside another task's critical section. */
  _Static_assert(configLIBRARY_LOWEST_INTERRUPT_PRIORITY <
                     (1u << __NVIC_PRIO_BITS),
                 "pass NVIC_SetPriority a raw priority, not a shifted one");
  NVIC_SetPriority(portNRF_RTC_IRQn, configLIBRARY_LOWEST_INTERRUPT_PRIORITY);
  NVIC_ClearPendingIRQ(portNRF_RTC_IRQn);
  NVIC_EnableIRQ(portNRF_RTC_IRQn);

  portNRF_RTC_REG->TASKS_START = 1u;
}

void RTC1_IRQHandler(void) {
  if (portNRF_RTC_REG->EVENTS_OVRFLW != 0u) {
    portNRF_RTC_REG->EVENTS_OVRFLW = 0u;
    slate_rtc1_on_overflow();
  }

#if (configUSE_TICKLESS_IDLE == 1)
  if (portNRF_RTC_REG->EVENTS_COMPARE[0] != 0u) {
    portNRF_RTC_REG->EVENTS_COMPARE[0] = 0u;
  }
#endif

  if (portNRF_RTC_REG->EVENTS_TICK == 0u) {
    return;
  }
  portNRF_RTC_REG->EVENTS_TICK = 0u;

  portDISABLE_INTERRUPTS();

  /* Advance to wherever COUNTER actually is (see header). Guards, in order:
   * a diff in the top half of the 24-bit range means the tick sits AHEAD of
   * COUNTER (only reachable via tickless vTaskStepTick) — step once, never
   * ~16M; diff 0 means the event beat the counter read — step once; and the
   * catch-up is capped so the ISR stays short. */
  uint32_t diff = (portNRF_RTC_REG->COUNTER - (uint32_t)xTaskGetTickCountFromISR()) &
                  portNRF_RTC_MAXTICKS;
  if (diff > (portNRF_RTC_MAXTICKS >> 1) || diff == 0u) {
    diff = 1u;
  } else if (diff > kMaxCatchUp) {
    diff = kMaxCatchUp;
  }
  if (diff > 1u) {
    s_catchup_ticks += diff - 1u;
  }

  BaseType_t switch_req = pdFALSE;
  while (diff-- > 0u) {
    if (xTaskIncrementTick() != pdFALSE) {
      switch_req = pdTRUE;
    }
  }
  if (switch_req != pdFALSE) {
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    __SEV();
  }
  portENABLE_INTERRUPTS();
}

#if (configUSE_TICKLESS_IDLE == 1)

void vPortSuppressTicksAndSleep(TickType_t xExpectedIdleTime) {
  TickType_t enterTime;

  if (xExpectedIdleTime >
      portNRF_RTC_MAXTICKS - configEXPECTED_IDLE_TIME_BEFORE_SLEEP) {
    xExpectedIdleTime =
        portNRF_RTC_MAXTICKS - configEXPECTED_IDLE_TIME_BEFORE_SLEEP;
  }

  __disable_irq();
  enterTime = portNRF_RTC_REG->COUNTER;

  if (eTaskConfirmSleepModeStatus() == eAbortSleep) {
    __enable_irq();
    return;
  }

  TickType_t xModifiableIdleTime = xExpectedIdleTime;
  const TickType_t wakeupTime =
      (enterTime + xExpectedIdleTime) & portNRF_RTC_MAXTICKS;

  portNRF_RTC_REG->INTENCLR = RTC_INTENCLR_TICK_Msk;
  portNRF_RTC_REG->CC[0] = wakeupTime;
  portNRF_RTC_REG->EVENTS_COMPARE[0] = 0u;
  portNRF_RTC_REG->INTENSET = RTC_INTENSET_COMPARE0_Msk;

  __DSB();

  configPRE_SLEEP_PROCESSING(xModifiableIdleTime);
  if (xModifiableIdleTime > 0) {
    __set_FPSCR(__get_FPSCR() & ~(FPU_EXCEPTION_MASK));
    (void)__get_FPSCR();
    NVIC_ClearPendingIRQ(FPU_IRQn);

    do {
      __WFE();
    } while ((NVIC->ISPR[0] | NVIC->ISPR[1]) == 0u);
  }
  configPOST_SLEEP_PROCESSING(xExpectedIdleTime);

  portNRF_RTC_REG->INTENCLR = RTC_INTENCLR_COMPARE0_Msk;
  portNRF_RTC_REG->EVENTS_COMPARE[0] = 0u;
  portNRF_RTC_REG->EVENTS_TICK = 0u;
  portNRF_RTC_REG->INTENSET = RTC_INTENSET_TICK_Msk;

  {
    const TickType_t exitTime = portNRF_RTC_REG->COUNTER;
    TickType_t diff =
        (TickType_t)((exitTime - enterTime) & portNRF_RTC_MAXTICKS);
    NVIC_ClearPendingIRQ(portNRF_RTC_IRQn);
    if (diff > xExpectedIdleTime) {
      diff = xExpectedIdleTime;
    }

    /* Adafruit / Nordic Q&A 63828: step (diff-1) then one IncrementTick so
     * the RTOS tick lands on exitTime without leaving a pending TICK that
     * would immediately double-count. */
    BaseType_t switch_req = pdFALSE;
    if (diff > 1u) {
      vTaskStepTick(diff - 1u);
      switch_req = xTaskIncrementTick();
    } else if (diff == 1u) {
      switch_req = xTaskIncrementTick();
    }
    if (switch_req != pdFALSE) {
      SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
      __SEV();
    }
  }

  __enable_irq();
}

#endif /* configUSE_TICKLESS_IDLE */
