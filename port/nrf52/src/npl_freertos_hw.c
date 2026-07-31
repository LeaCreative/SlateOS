#include "nrf.h"

/* Declared in nimble/npl_freertos.h; implement without pulling that header
 * (it depends on ble_npl types from nimble_npl_os.h). */
void npl_freertos_hw_set_isr(int irqn, void (*addr)(void));

/* NimBLE controller installs RADIO / TIMER ISRs at runtime. VTOR must point at
 * a RAM vector table (Slate relocates it in SystemInit / Reset_Handler). */
void npl_freertos_hw_set_isr(int irqn, void (*addr)(void)) {
  NVIC_SetVector((IRQn_Type)irqn, (uint32_t)addr);
}
