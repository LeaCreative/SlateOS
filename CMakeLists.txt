cmake_minimum_required(VERSION 3.21)

project(slate_firmware LANGUAGES C CXX ASM)

option(BOOTLOADER_PRESENT "Link application after MCUBoot header (0x8020)" ON)
option(SLATE_ENABLE_RTT "Enable SEGGER RTT logging" ON)
# Default ON when MCUBoot slot is used (sealed DFU). Bring-up can force OFF.
option(SLATE_HAS_NIMBLE "Link Apache NimBLE + FreeRTOS BLE stack" ON)
option(SLATE_BLE_TRANSPORT "Build SDP framing + BLE link (M5)" ON)
option(SLATE_POWER_INSTRUM "Log power-state transitions with timestamps" ON)

# Sealed-watch boot bisect (debug only). 0=off; 1=confirm; 2=FreeRTOS; 3=NimBLE.
# Confirm-first forfeits MCUBoot auto-revert — recover with pinecone blue button.
set(SLATE_BISECT_STAGE 0 CACHE STRING
  "Boot bisect stage: 0=off, 1=confirm-first bare metal, 2=FreeRTOS, 3=NimBLE")

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

set(SLATE_BISECT_FREERTOS OFF)
if(SLATE_BISECT_STAGE GREATER 0)
  set(BOOTLOADER_PRESENT ON CACHE BOOL
    "Forced ON for sealed bisect DFU" FORCE)
  message(WARNING
    "SLATE_BISECT_STAGE=${SLATE_BISECT_STAGE}: timed pass colour then WDT "
    "release back to InfiniTime. Do not ship bisect images.")
  if(SLATE_BISECT_STAGE EQUAL 1)
    set(SLATE_HAS_NIMBLE OFF CACHE BOOL "" FORCE)
  elseif(SLATE_BISECT_STAGE EQUAL 2)
    set(SLATE_HAS_NIMBLE OFF CACHE BOOL "" FORCE)
    set(SLATE_BISECT_FREERTOS ON)
  elseif(SLATE_BISECT_STAGE EQUAL 3)
    set(SLATE_HAS_NIMBLE ON CACHE BOOL "" FORCE)
  else()
    message(FATAL_ERROR "SLATE_BISECT_STAGE must be 0, 1, 2, or 3")
  endif()
endif()

# Flash map: include/flash_map.hpp / docs/ota.md
# With MCUBoot: link at slot+header (0x8020), same as InfiniTime APP_OFFSET.
# imgtool --pad-header prepends 32B; vectors must already assume 0x8020 base.
set(SLATE_FLASH_ORIGIN 0x00000000)
set(SLATE_FLASH_LENGTH 0x00080000)
if(BOOTLOADER_PRESENT)
  set(SLATE_FLASH_ORIGIN 0x00008020)  # 0x8000 + 32B MCUBoot header
  set(SLATE_FLASH_LENGTH 0x00073FE0)  # 0x74000 - 0x20
endif()
set(SLATE_PERSIST_FLASH_ADDR 0x0007F000)

set(SLATE_RAM_ORIGIN 0x20000000)
set(SLATE_RAM_LENGTH 0x00010000)
set(SLATE_STACK_SIZE 0x00001000)
set(SLATE_HEAP_SIZE 0x00000800)
if(SLATE_HAS_NIMBLE AND SLATE_BISECT_STAGE EQUAL 0)
  # Production: MSP 4 KB for nested NimBLE IRQs; newlib heap unused (heap_4).
  set(SLATE_STACK_SIZE 0x00001000)
  set(SLATE_HEAP_SIZE 0x00000200)
elseif(SLATE_BISECT_STAGE GREATER 0)
  # Bisect always uses 4 KB MSP so stack size is not a confounder.
  set(SLATE_STACK_SIZE 0x00001000)
  set(SLATE_HEAP_SIZE 0x00000200)
endif()

set(LINKER_SCRIPT "${CMAKE_BINARY_DIR}/slate_nrf52832.ld")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/linker/nrf52832_app.ld.in"
  "${LINKER_SCRIPT}"
  @ONLY
)

set(SLATE_APP_SOURCES
  src/startup.cpp
  src/system_nrf52832.cpp
  src/rtt.cpp
  src/spi_bus.cpp
  src/st7789.cpp
  src/renderer.cpp
  src/backlight.cpp
  src/twi.cpp
  src/cst816s.cpp
  src/motor.cpp
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
  src/mono_time.cpp
  src/rtc_hw.cpp
  src/notif_store.cpp
  src/alarm_sched.cpp
  src/bma42x.cpp
  src/raise_wake.cpp
  src/shake_wake.cpp
  src/hrs3300.cpp
  src/ppg.cpp
  src/hr_controller.cpp
  src/hr_task.cpp
  src/settings_sync.cpp
  third_party/bosch/bma_config.cpp
  src/battery.cpp
  src/battery_hw.cpp
  src/local_ui.cpp
  src/local_core.cpp
  src/xt25.cpp
  src/lfs_fs.cpp
  src/asset_pack.cpp
  src/glyph_cache.cpp
  src/asset_xfer.cpp
  src/power.cpp
  src/ota_slot.cpp
  src/ota_xfer.cpp
  src/sha256.cpp
  src/boot_util.cpp
  src/dfu_nordic.cpp
  src/wdt.cpp
  src/mcuboot_pubkey.cpp
  third_party/littlefs/lfs.c
  third_party/littlefs/lfs_util.c
  src/ble_link.cpp
  src/ble_conn.cpp
  src/ble_gatt.cpp
  src/ble_mbuf_stats.cpp
  src/ble_nimble.cpp
  src/freertos_smoke.cpp
)

# boot_diag is in every build: sealed watches have no SWD/RTT, so on-screen
# fatal colours are the only post-mortem channel.
list(APPEND SLATE_APP_SOURCES src/boot_diag.cpp)
if(SLATE_BISECT_STAGE GREATER 0)
  list(APPEND SLATE_APP_SOURCES src/main_bisect.cpp)
else()
  list(APPEND SLATE_APP_SOURCES src/main.cpp)
endif()

add_executable(slate_firmware.elf ${SLATE_APP_SOURCES})

target_include_directories(slate_firmware.elf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/littlefs"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bosch"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/arduinoFFT/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/shared/generated"
)

target_compile_definitions(slate_firmware.elf PRIVATE
  NRF52832_XXAA=1
  SLATE_PERSIST_FLASH_ADDR=${SLATE_PERSIST_FLASH_ADDR}
  $<$<BOOL:${SLATE_ENABLE_RTT}>:SLATE_ENABLE_RTT=1>
  $<$<BOOL:${SLATE_POWER_INSTRUM}>:SLATE_POWER_INSTRUM=1>
  $<$<NOT:$<BOOL:${SLATE_POWER_INSTRUM}>>:SLATE_POWER_INSTRUM=0>
  $<$<CONFIG:Debug>:SLATE_LOG_LEVEL=4>
  $<$<CONFIG:Release>:SLATE_LOG_LEVEL=2>
  $<$<CONFIG:Debug>:SLATE_BLE_DIAG=1>
  $<$<CONFIG:Release>:SLATE_BLE_DIAG=0>
  LFS_NO_MALLOC=1
  LFS_NO_DEBUG=1
  LFS_NO_WARN=1
  LFS_NO_ERROR=1
  LFS_NO_ASSERT=1
)

if(SLATE_BISECT_STAGE GREATER 0)
  target_compile_definitions(slate_firmware.elf PRIVATE
    SLATE_BISECT_STAGE=${SLATE_BISECT_STAGE}
  )
endif()

if(SLATE_HAS_NIMBLE)
  include("${CMAKE_SOURCE_DIR}/cmake/Nimble.cmake")
elseif(SLATE_BISECT_FREERTOS)
  include("${CMAKE_SOURCE_DIR}/cmake/FreertosOnly.cmake")
else()
  target_compile_definitions(slate_firmware.elf PRIVATE SLATE_HAS_NIMBLE=0)
endif()

target_compile_options(slate_firmware.elf PRIVATE
  -mcpu=cortex-m4
  -mthumb
  -mabi=aapcs
  -mfpu=fpv4-sp-d16
  -mfloat-abi=hard
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
  # Do not quote the path here — CMake/Ninja on Windows otherwise passes
  # literal \"...\" to ld and the script open fails with Invalid argument.
  -T${LINKER_SCRIPT}
  -mcpu=cortex-m4
  -mthumb
  -mabi=aapcs
  -mfpu=fpv4-sp-d16
  -mfloat-abi=hard
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  -Wl,-Map=${CMAKE_BINARY_DIR}/slate_firmware.map
  -nostartfiles
  --specs=nano.specs
  --specs=nosys.specs
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

# InfiniTime-compatible DFU zip (imgtool create + adafruit-nrfutil). Optional tools.
# MCUBoot --version is derived from include/slate_version.hpp (`0.1.0-mN` → 0.1.N).
# Do not hardcode it here — keep it in lockstep with kVersion.
find_program(SLATE_PYTHON NAMES python3 python py)
if(SLATE_PYTHON AND BOOTLOADER_PRESENT)
  add_custom_target(slate_dfu
    COMMAND "${SLATE_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/package_dfu.py"
            --bin "${CMAKE_BINARY_DIR}/slate_firmware.bin"
            --out-dir "${CMAKE_BINARY_DIR}"
    DEPENDS slate_firmware.elf
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Packaging InfiniTime-compatible slate-dfu.zip"
  )
endif()
