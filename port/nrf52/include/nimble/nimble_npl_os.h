#pragma once

/*
 * Shadows mynewt-nimble's FreeRTOS NPL header. port/nrf52/include is added to
 * the target with BEFORE, so this file wins and pulls the stock one in with
 * #include_next.
 *
 * Upstream implements the NPL hardware critical section as:
 *
 *     ble_npl_hw_enter_critical() { vPortEnterCritical(); return 0; }
 *
 * vPortEnterCritical() is the *task level* FreeRTOS critical section and
 * asserts when entered from an exception (port.c checks VECTACTIVE). The link
 * layer takes this lock inside the RADIO and TIMER0 ISRs, so a controller build
 * faults as soon as the radio starts. Mynewt's own NPL saves and restores
 * PRIMASK instead; do the same here.
 *
 * Masking PRIMASK also blocks SysTick and PendSV, so this still serialises
 * against the scheduler the way the link layer expects.
 */

#include "nrf.h"

/* Rename the stock definitions out of the way rather than editing the
   submodule. They are unused; only these names change, not the call sites. */
#define ble_npl_hw_enter_critical npl_upstream_hw_enter_critical
#define ble_npl_hw_exit_critical  npl_upstream_hw_exit_critical
#include_next <nimble/nimble_npl_os.h>
#undef ble_npl_hw_enter_critical
#undef ble_npl_hw_exit_critical

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t ble_npl_hw_enter_critical(void) {
  uint32_t ctx = __get_PRIMASK();
  __disable_irq();
  return ctx & 1u;
}

static inline void ble_npl_hw_exit_critical(uint32_t ctx) {
  if (!ctx) {
    __enable_irq();
  }
}

#ifdef __cplusplus
}
#endif
