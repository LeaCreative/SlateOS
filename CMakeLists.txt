cmake_minimum_required(VERSION 3.21)

project(slate_firmware LANGUAGES C CXX ASM)

option(BOOTLOADER_PRESENT "Link application above a 32KB bootloader" OFF)
option(SLATE_ENABLE_RTT "Enable SEGGER RTT logging" ON)

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
  src/sdp_validate.cpp
  src/sdp_parser.cpp
  src/sdp_interpreter.cpp
  src/main.cpp
)

target_include_directories(slate_firmware.elf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(slate_firmware.elf PRIVATE
  NRF52832_XXAA=1
  $<$<BOOL:${SLATE_ENABLE_RTT}>:SLATE_ENABLE_RTT=1>
  $<$<CONFIG:Debug>:SLATE_LOG_LEVEL=4>
  $<$<CONFIG:Release>:SLATE_LOG_LEVEL=2>
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
