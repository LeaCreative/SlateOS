cmake_minimum_required(VERSION 3.21)

project(slate_firmware LANGUAGES C CXX ASM)

option(BOOTLOADER_PRESENT "Link application above a 32KB bootloader" OFF)
option(SLATE_ENABLE_RTT "Enable SEGGER RTT logging" ON)
option(SLATE_HAS_NIMBLE "Link Apache NimBLE + FreeRTOS BLE stack" OFF)
option(SLATE_BLE_TRANSPORT "Build SDP framing + BLE link (M5)" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
  message(FATAL_ERROR
    "Configure with the ARM toolchain file, e.g.:\n"
    "  cmake -S . -B build/debug -G Ninja "
    "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake "
    "-DCMAKE_BUILD_TYPE=Debug")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(SLATE_FLASH_ORIGIN 0x00000000)
set(SLATE_FLASH_LENGTH 0x00080000)
if(BOOTLOADER_PRESENT)
  set(SLATE_FLASH_ORIGIN 0x00008000)
  math(EXPR SLATE_FLASH_LENGTH "0x80000 - 0x8000" OUTPUT_FORMAT HEXADECIMAL)
endif()
# Reserve last 4 KB of the chip for NVMC persist (clock / alarms / settings).
math(EXPR SLATE_FLASH_LENGTH "${SLATE_FLASH_LENGTH} - 0x1000" OUTPUT_FORMAT HEXADECIMAL)
set(SLATE_PERSIST_FLASH_ADDR 0x0007F000)

set(SLATE_RAM_ORIGIN 0x20000000)
set(SLATE_RAM_LENGTH 0x00010000)
set(SLATE_STACK_SIZE 0x00001000)
set(SLATE_HEAP_SIZE 0x00000800)

set(LINKER_SCRIPT "${CMAKE_BINARY_DIR}/slate_nrf52832.ld")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/linker/nrf52832_app.ld.in"
  "${LINKER_SCRIPT}"
  @ONLY
)

add_executable(slate_firmware.elf
  src/startup.cpp
  src/system_nrf52832.cpp
  src/rtt.cpp
  src/spi_bus.cpp
  src/st7789.cpp
  src/renderer.cpp
  src/backlight.cpp
  src/twi.cpp
  src/cst816s.cpp
  src/button.cpp
  src/input_event.cpp
  src/input_router.cpp
  src/sdp_validate.cpp
  src/sdp_parser.cpp
  src/sdp_interpreter.cpp
  src/sdp_frame.cpp
  src/sdp_diag.cpp
  src/session.cpp
  src/persist.cpp
  src/persist_nvmc.cpp
  src/wall_clock.cpp
  src/rtc_hw.cpp
  src/notif_store.cpp
  src/alarm_sched.cpp
  src/bma42x.cpp
  src/battery.cpp
  src/battery_hw.cpp
  src/local_ui.cpp
  src/local_core.cpp
  src/ble_link.cpp
  src/ble_conn.cpp
  src/ble_gatt.cpp
  src/ble_mbuf_stats.cpp
  src/ble_nimble.cpp
  src/main.cpp
)

target_include_directories(slate_firmware.elf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
)

target_compile_definitions(slate_firmware.elf PRIVATE
  NRF52832_XXAA=1
  SLATE_PERSIST_FLASH_ADDR=${SLATE_PERSIST_FLASH_ADDR}
  $<$<BOOL:${SLATE_ENABLE_RTT}>:SLATE_ENABLE_RTT=1>
  $<$<BOOL:${SLATE_HAS_NIMBLE}>:SLATE_HAS_NIMBLE=1>
  $<$<NOT:$<BOOL:${SLATE_HAS_NIMBLE}>>:SLATE_HAS_NIMBLE=0>
  $<$<CONFIG:Debug>:SLATE_LOG_LEVEL=4>
  $<$<CONFIG:Release>:SLATE_LOG_LEVEL=2>
  $<$<CONFIG:Debug>:SLATE_BLE_DIAG=1>
  $<$<CONFIG:Release>:SLATE_BLE_DIAG=0>
)

target_compile_options(slate_firmware.elf PRIVATE
  -mcpu=cortex-m4
  -mthumb
  -mabi=aapcs
  -ffreestanding
  -fno-exceptions
  -fno-rtti
  -fno-use-cxa-atexit
  -fdata-sections
  -ffunction-sections
  -fno-threadsafe-statics
  -Wall
  -Wextra
  -Wpedantic
  $<$<CONFIG:Debug>:-Og>
  $<$<CONFIG:Debug>:-g3>
  $<$<CONFIG:Release>:-Os>
)

target_link_options(slate_firmware.elf PRIVATE
  -T"${LINKER_SCRIPT}"
  -mcpu=cortex-m4
  -mthumb
  -mabi=aapcs
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/slate_firmware.map
  -nostartfiles
)

find_program(CMAKE_OBJCOPY arm-none-eabi-objcopy REQUIRED)
find_program(CMAKE_SIZE arm-none-eabi-size REQUIRED)

add_custom_command(TARGET slate_firmware.elf POST_BUILD
  COMMAND "${CMAKE_OBJCOPY}" -O ihex
          "$<TARGET_FILE:slate_firmware.elf>"
          "${CMAKE_BINARY_DIR}/slate_firmware.hex"
  COMMAND "${CMAKE_OBJCOPY}" -O binary
          "$<TARGET_FILE:slate_firmware.elf>"
          "${CMAKE_BINARY_DIR}/slate_firmware.bin"
  COMMAND "${CMAKE_SIZE}" --format=berkeley "$<TARGET_FILE:slate_firmware.elf>"
  BYPRODUCTS
    "${CMAKE_BINARY_DIR}/slate_firmware.hex"
    "${CMAKE_BINARY_DIR}/slate_firmware.bin"
  COMMENT "Generating HEX/BIN images"
)
