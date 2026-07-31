# Slate NimBLE + FreeRTOS integration (M5 radio completion).
# Included from root CMakeLists when SLATE_HAS_NIMBLE=ON.

set(NIMBLE_ROOT "${CMAKE_SOURCE_DIR}/third_party/mynewt-nimble")
set(FREERTOS_ROOT "${CMAKE_SOURCE_DIR}/third_party/FreeRTOS-Kernel")
set(NRFX_ROOT "${CMAKE_SOURCE_DIR}/third_party/nrfx")
set(CMSIS_CORE "${CMAKE_SOURCE_DIR}/third_party/CMSIS_5/CMSIS/Core/Include")
set(SLATE_PORT_NRF52 "${CMAKE_SOURCE_DIR}/port/nrf52")

if(NOT EXISTS "${NIMBLE_ROOT}/nimble/host/src/ble_hs.c")
  message(FATAL_ERROR
    "NimBLE submodule missing. Run:\n"
    "  git submodule update --init --recursive")
endif()

# ── FreeRTOS kernel ──────────────────────────────────────────────────────────
set(FREERTOS_SOURCES
  "${FREERTOS_ROOT}/tasks.c"
  "${FREERTOS_ROOT}/queue.c"
  "${FREERTOS_ROOT}/list.c"
  "${FREERTOS_ROOT}/timers.c"
  "${FREERTOS_ROOT}/event_groups.c"
  "${FREERTOS_ROOT}/stream_buffer.c"
  "${FREERTOS_ROOT}/portable/MemMang/heap_4.c"
  "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F/port.c"
)

# ── NimBLE porting layer + host + controller + nRF5x PHY ─────────────────────
file(GLOB NIMBLE_PORTING_SRC "${NIMBLE_ROOT}/porting/nimble/src/*.c")
file(GLOB NIMBLE_HOST_SRC "${NIMBLE_ROOT}/nimble/host/src/*.c")
file(GLOB NIMBLE_HOST_UTIL_SRC "${NIMBLE_ROOT}/nimble/host/util/src/*.c")
file(GLOB NIMBLE_TRANSPORT_SRC "${NIMBLE_ROOT}/nimble/transport/src/*.c")
file(GLOB NIMBLE_TRANSPORT_RAM_SRC "${NIMBLE_ROOT}/nimble/transport/ram/src/*.c")
file(GLOB NIMBLE_CONTROLLER_SRC "${NIMBLE_ROOT}/nimble/controller/src/*.c")
file(GLOB NIMBLE_NRF5X_SRC "${NIMBLE_ROOT}/nimble/drivers/nrf5x/src/*.c")
file(GLOB NIMBLE_NRF5X_NRF52_SRC "${NIMBLE_ROOT}/nimble/drivers/nrf5x/src/nrf52/*.c")
file(GLOB NIMBLE_NPL_FREERTOS_SRC "${NIMBLE_ROOT}/porting/npl/freertos/src/*.c")

set(NIMBLE_SVC_SRC
  "${NIMBLE_ROOT}/nimble/host/services/gap/src/ble_svc_gap.c"
  "${NIMBLE_ROOT}/nimble/host/services/gatt/src/ble_svc_gatt.c"
  "${NIMBLE_ROOT}/nimble/host/services/bas/src/ble_svc_bas.c"
  "${NIMBLE_ROOT}/nimble/host/services/dis/src/ble_svc_dis.c"
  "${NIMBLE_ROOT}/nimble/host/store/ram/src/ble_store_ram.c"
)

file(GLOB NIMBLE_EXT_TINYCRYPT_SRC "${NIMBLE_ROOT}/ext/tinycrypt/src/*.c")

# Do not compile nrfx mdk/system_nrf52.c — Slate owns SystemInit in
# src/system_nrf52832.cpp (UICR NFCPINS, FPU, VTOR-to-RAM).
set(NRFX_SOURCES
  "${NRFX_ROOT}/drivers/src/nrfx_clock.c"
  "${NRFX_ROOT}/drivers/src/nrfx_nvmc.c"
  "${NRFX_ROOT}/soc/nrfx_atomic.c"
)

set(SLATE_NIMBLE_SOURCES
  ${FREERTOS_SOURCES}
  ${NIMBLE_PORTING_SRC}
  ${NIMBLE_HOST_SRC}
  ${NIMBLE_HOST_UTIL_SRC}
  ${NIMBLE_TRANSPORT_SRC}
  ${NIMBLE_TRANSPORT_RAM_SRC}
  ${NIMBLE_CONTROLLER_SRC}
  ${NIMBLE_NRF5X_SRC}
  ${NIMBLE_NRF5X_NRF52_SRC}
  ${NIMBLE_NPL_FREERTOS_SRC}
  ${NIMBLE_SVC_SRC}
  ${NIMBLE_EXT_TINYCRYPT_SRC}
  ${NRFX_SOURCES}
  "${SLATE_PORT_NRF52}/src/slate_ble_addr.c"
  "${SLATE_PORT_NRF52}/src/npl_freertos_hw.c"
  "${SLATE_PORT_NRF52}/src/port_rtc_tick.c"
)

target_sources(slate_firmware.elf PRIVATE ${SLATE_NIMBLE_SOURCES})

# Port include first so nimble_npl_os_log.h / nrfx_config override stock headers.
target_include_directories(slate_firmware.elf BEFORE PRIVATE
  "${SLATE_PORT_NRF52}/include"
)
target_include_directories(slate_firmware.elf PRIVATE
  "${CMSIS_CORE}"
  "${NRFX_ROOT}"
  "${NRFX_ROOT}/mdk"
  "${NRFX_ROOT}/hal"
  "${NRFX_ROOT}/drivers/include"
  "${NRFX_ROOT}/soc"
  "${FREERTOS_ROOT}/include"
  "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F"
  "${CMAKE_SOURCE_DIR}/config"
  "${NIMBLE_ROOT}/nimble/include"
  "${NIMBLE_ROOT}/nimble/host/include"
  "${NIMBLE_ROOT}/nimble/host/services/gap/include"
  "${NIMBLE_ROOT}/nimble/host/services/gatt/include"
  "${NIMBLE_ROOT}/nimble/host/services/bas/include"
  "${NIMBLE_ROOT}/nimble/host/services/dis/include"
  "${NIMBLE_ROOT}/nimble/host/store/ram/include"
  "${NIMBLE_ROOT}/nimble/host/util/include"
  "${NIMBLE_ROOT}/nimble/transport/include"
  "${NIMBLE_ROOT}/nimble/transport/ram/include"
  "${NIMBLE_ROOT}/nimble/controller/include"
  "${NIMBLE_ROOT}/nimble/drivers/nrf5x/include"
  "${NIMBLE_ROOT}/porting/nimble/include"
  "${NIMBLE_ROOT}/porting/npl/freertos/include"
  "${NIMBLE_ROOT}/ext/tinycrypt/include"
)

target_compile_definitions(slate_firmware.elf PRIVATE
  SLATE_HAS_NIMBLE=1
  SLATE_HAS_FREERTOS=1
  # NimBLE's own switch. ble_phy.c only picks a FreeRTOS-safe RADIO_IRQn
  # priority (5) when this is defined; otherwise it uses 0, which is above
  # configMAX_SYSCALL_INTERRUPT_PRIORITY and trips vPortValidateInterruptPriority
  # the first time the radio ISR posts to an event queue.
  FREERTOS=1
  SLATE_FREERTOS_RTC_TICK=1
  NIMBLE_CFG_CONTROLLER=1
  NRF52832_XXAA=1
  NRF52=1
  NRF52_SERIES=1
  __HEAP_SIZE=0
  __STACK_SIZE=0
)

# Quiet third-party noise; force Slate syscfg only on NimBLE/nrfx/FreeRTOS TUs.
foreach(src ${SLATE_NIMBLE_SOURCES})
  set_source_files_properties("${src}" PROPERTIES
    COMPILE_FLAGS "-include ${SLATE_PORT_NRF52}/include/slate_syscfg.h -Wno-pedantic -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-implicit-function-declaration"
  )
endforeach()
