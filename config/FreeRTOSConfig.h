#ifndef SLATE_FREERTOS_CONFIG_H
#define SLATE_FREERTOS_CONFIG_H

#include <stdint.h>
#include "nrf.h"

// FreeRTOS config for Slate on nRF52832 (Cortex-M4F).
// OS tick: RTC1 @ configTICK_RATE_HZ from LFXO (InfiniTime / Nordic pattern).
// RTC0 remains NimBLE os_cputime. Wall clock shares RTC1 COUNTER.
// Heap must cover: app (~3KB) + NimBLE ll/ble (~2KB each) + idle/timer + NPL
// queues + BAS/DIS. 14KB left ~4KB after task stacks — xQueueCreate for the
// NimBLE default eventq then returned NULL and configASSERT'd in queue.c:1513
// (cyan on sealed watch). 16KB matches measured need; 18KB is bisect-only.

#define configCPU_CLOCK_HZ              (64000000UL)
#define configTICK_RATE_HZ              (1024)
#define configMAX_PRIORITIES            (5)
#define configMINIMAL_STACK_SIZE        (128)
#define configTOTAL_HEAP_SIZE           (16 * 1024)
#define configMAX_TASK_NAME_LEN         (12)
#define configUSE_PREEMPTION            1
#define configUSE_IDLE_HOOK             1
// Tick hook measures app_loop stalls for diag only — it does not pet the WDT
// (InfiniTime: reload from SystemTask / app task alone).
#define configUSE_TICK_HOOK             1
#define configUSE_16_BIT_TICKS          0
#define configUSE_MUTEXES               1
#define configUSE_RECURSIVE_MUTEXES     1
#define configUSE_COUNTING_SEMAPHORES   1
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configUSE_TIMERS                1
#define configTIMER_TASK_PRIORITY       (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH        8
#define configTIMER_TASK_STACK_DEPTH    256

// RTC1 FreeRTOS tick (see port/nrf52/src/port_rtc_tick.c). SysTick cannot
// drive tickless idle on nRF52 — the core clock is gated in System ON sleep.
#define SLATE_FREERTOS_RTC_TICK         1
#define configSYSTICK_CLOCK_HZ          (32768UL)
// Tickless stays off until the RTC sleep path is soak-tested on sealed
// hardware. The first enable hung the tick ISR after ~1 s (COUNTER/tick
// underflow catch-up) and the bootloader WDT reverted at ~7 s. RTC1 still
// owns the tick and wall clock; idle current remains high until this flips.
#define configUSE_TICKLESS_IDLE         0
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP  2

#if (configUSE_TICKLESS_IDLE == 1) && !defined(SLATE_FREERTOS_RTC_TICK)
#error "tickless idle needs an RTC tick source on nRF52 (SysTick halts in sleep)"
#endif

// Cap a single tickless sleep so the bootloader's ~7 s watchdog is always
// reloaded from configPOST_SLEEP_PROCESSING inside the window.
#define slateMAX_TICKLESS_IDLE_TICKS    (configTICK_RATE_HZ)  // 1 s

#ifdef __cplusplus
extern "C" {
#endif
void slate_wdt_pet(void);
#ifdef __cplusplus
}
#endif

#define configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING(x) \
  do {                                                  \
    if ((x) > slateMAX_TICKLESS_IDLE_TICKS) {            \
      (x) = slateMAX_TICKLESS_IDLE_TICKS;                \
    }                                                   \
  } while (0)

#define configPOST_SLEEP_PROCESSING(x) slate_wdt_pet()

#define configCHECK_FOR_STACK_OVERFLOW  2
#define configUSE_MALLOC_FAILED_HOOK    1

// Bisect stages keep a larger heap; tickless stays on once RTC tick is present.
#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE > 0)
#undef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE           (18 * 1024)
#endif

#define configASSERT(x)                 \
  if ((x) == 0) {                       \
    vAssertCalled(__FILE__, __LINE__);  \
  }
void vAssertCalled(const char* file, int line);

#define INCLUDE_vTaskDelay              1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_vTaskSuspend            1

#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 3
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      7
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 3
#define configKERNEL_INTERRUPT_PRIORITY \
  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Map FreeRTOS handlers onto CMSIS names used in Slate's vector table.
 * The OS tick is RTC1_IRQHandler (port_rtc_tick.c), not SysTick. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif
