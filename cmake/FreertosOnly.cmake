# FreeRTOS kernel only (bisect stage 2 — no NimBLE).
# Included from root CMakeLists when SLATE_BISECT_FREERTOS=ON.

set(FREERTOS_ROOT "${CMAKE_SOURCE_DIR}/third_party/FreeRTOS-Kernel")
set(CMSIS_CORE "${CMAKE_SOURCE_DIR}/third_party/CMSIS_5/CMSIS/Core/Include")
set(NRFX_ROOT "${CMAKE_SOURCE_DIR}/third_party/nrfx")
set(SLATE_PORT_NRF52 "${CMAKE_SOURCE_DIR}/port/nrf52")

set(FREERTOS_SOURCES
  "${FREERTOS_ROOT}/tasks.c"
  "${FREERTOS_ROOT}/queue.c"
  "${FREERTOS_ROOT}/list.c"
  "${FREERTOS_ROOT}/timers.c"
  "${FREERTOS_ROOT}/event_groups.c"
  "${FREERTOS_ROOT}/stream_buffer.c"
  "${FREERTOS_ROOT}/portable/MemMang/heap_4.c"
  "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F/port.c"
  "${SLATE_PORT_NRF52}/src/port_rtc_tick.c"
)

target_sources(slate_firmware.elf PRIVATE ${FREERTOS_SOURCES})

target_include_directories(slate_firmware.elf PRIVATE
  "${CMSIS_CORE}"
  "${NRFX_ROOT}/mdk"
  "${FREERTOS_ROOT}/include"
  "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F"
  "${CMAKE_SOURCE_DIR}/config"
)

target_compile_definitions(slate_firmware.elf PRIVATE
  SLATE_HAS_NIMBLE=0
  SLATE_HAS_FREERTOS=1
  SLATE_FREERTOS_RTC_TICK=1
  NRF52832_XXAA=1
  NRF52=1
  NRF52_SERIES=1
)

foreach(src ${FREERTOS_SOURCES})
  set_source_files_properties("${src}" PROPERTIES
    COMPILE_FLAGS "-Wno-pedantic -Wno-unused-parameter"
  )
endforeach()
