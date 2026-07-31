#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>

#include "nrf.h"

#define NRFX_ASSERT(expression) assert(expression)
#define NRFX_STATIC_ASSERT(expression) _Static_assert(expression, #expression)

#define NRFX_IRQ_PRIORITY_SET(irq_number, priority) \
  NVIC_SetPriority((IRQn_Type)(irq_number), (priority))
#define NRFX_IRQ_ENABLE(irq_number) NVIC_EnableIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_IS_ENABLED(irq_number) NVIC_GetEnableIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_DISABLE(irq_number) NVIC_DisableIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_PENDING_SET(irq_number) NVIC_SetPendingIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_PENDING_CLEAR(irq_number) NVIC_ClearPendingIRQ((IRQn_Type)(irq_number))
#define NRFX_IRQ_IS_PENDING(irq_number) NVIC_GetPendingIRQ((IRQn_Type)(irq_number))

#define NRFX_DELAY_US(us_time)                                     \
  do {                                                             \
    for (volatile uint32_t _i = 0; _i < ((us_time) * 16u); ++_i) { \
    }                                                              \
  } while (0)

#define NRFX_CRITICAL_SECTION_ENTER()       \
  uint32_t _nrfx_primask = __get_PRIMASK(); \
  __disable_irq()
#define NRFX_CRITICAL_SECTION_EXIT() __set_PRIMASK(_nrfx_primask)

#ifndef NRFX_CUSTOM_ERROR_CODES
#define NRFX_CUSTOM_ERROR_CODES 0
#endif

#define NRFX_EVENT_READBACK_ENABLED 1
#define NRFX_DPPI_CHANNELS_USED 0
#define NRFX_PPI_CHANNELS_USED 0
#define NRFX_PPI_GROUPS_USED 0
#define NRFX_GPIOTE_CHANNELS_USED 0
#define NRFX_EGUS_USED 0
#define NRFX_TIMERS_USED 0

#ifdef __cplusplus
}
#endif

#include "soc/nrfx_irqs.h"
#include "soc/nrfx_coredep.h"
#include "soc/nrfx_atomic.h"
