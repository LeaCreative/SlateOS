cmake_minimum_required(VERSION 3.21)

# Host-side unit tests — configure WITHOUT the ARM toolchain:
#   cmake -S tests/host -B build/host-tests
#   cmake --build build/host-tests
#   ctest --test-dir build/host-tests
#
# On Windows with llvm-mingw, test EXEs need libc++.dll / libunwind.dll.
# This file copies them next to the binaries and prepends the compiler bin
# dir to each test's PATH so Explorer / ctest / IDEs all work.

project(slate_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(SLATE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# ── llvm-mingw runtime DLLs (Windows) ────────────────────────────────────────
if(WIN32)
  get_filename_component(SLATE_HOST_CXX_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(SLATE_HOST_RUNTIME_DLLS
    "${SLATE_HOST_CXX_BIN}/libc++.dll"
    "${SLATE_HOST_CXX_BIN}/libunwind.dll"
    "${SLATE_HOST_CXX_BIN}/libwinpthread-1.dll"
  )
  set(_slate_copy_cmds)
  foreach(_dll IN LISTS SLATE_HOST_RUNTIME_DLLS)
    if(EXISTS "${_dll}")
      list(APPEND _slate_copy_cmds
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_dll}" "${CMAKE_BINARY_DIR}")
    endif()
  endforeach()
  if(_slate_copy_cmds)
    add_custom_target(slate_host_runtime_dlls ALL ${_slate_copy_cmds}
      COMMENT "Copy llvm-mingw runtime DLLs into ${CMAKE_BINARY_DIR}")
  endif()

  # ctest subprocesses inherit this PATH even if DLLs were not copied yet.
  set(SLATE_HOST_TEST_PATH "${SLATE_HOST_CXX_BIN};$ENV{PATH}")
  # Escape for CMake set_tests_properties ENVIRONMENT (semicolon-separated).
  string(REPLACE ";" "\\;" SLATE_HOST_TEST_PATH_ESC "${SLATE_HOST_TEST_PATH}")
endif()

function(slate_host_test exe_name test_name)
  add_test(NAME ${test_name} COMMAND ${exe_name})
  if(WIN32 AND DEFINED SLATE_HOST_TEST_PATH_ESC)
    set_tests_properties(${test_name} PROPERTIES
      ENVIRONMENT "PATH=${SLATE_HOST_TEST_PATH_ESC}")
  endif()
  if(TARGET slate_host_runtime_dlls)
    add_dependencies(${exe_name} slate_host_runtime_dlls)
  endif()
endfunction()

add_executable(test_hit_test
  test_hit_test.cpp
)
target_include_directories(test_hit_test PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_hit_test PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_hit_test hit_test)

add_executable(test_sdp_parser
  test_sdp_parser.cpp
  "${SLATE_ROOT}/src/sdp_validate.cpp"
  "${SLATE_ROOT}/src/sdp_parser.cpp"
)
target_include_directories(test_sdp_parser PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_sdp_parser PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_sdp_parser sdp_parser)

add_executable(test_renderer_cull
  test_renderer_cull.cpp
  "${SLATE_ROOT}/src/renderer.cpp"
)
target_include_directories(test_renderer_cull PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_renderer_cull PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_renderer_cull renderer_cull)

add_executable(test_uuids
  test_uuids.cpp
)
target_include_directories(test_uuids PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_uuids PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_uuids uuids)

add_executable(test_sdp_frame
  test_sdp_frame.cpp
  "${SLATE_ROOT}/src/sdp_frame.cpp"
)
target_include_directories(test_sdp_frame PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_sdp_frame PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_sdp_frame sdp_frame)

add_executable(test_ble_link
  test_ble_link.cpp
  rtt_stub.cpp
  "${SLATE_ROOT}/src/sdp_frame.cpp"
  "${SLATE_ROOT}/src/sdp_diag.cpp"
  "${SLATE_ROOT}/src/ble_link.cpp"
  "${SLATE_ROOT}/src/ble_conn.cpp"
  "${SLATE_ROOT}/src/ble_gatt.cpp"
  "${SLATE_ROOT}/src/freertos_smoke.cpp"
)
target_include_directories(test_ble_link PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_ble_link PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_ble_link ble_link)

add_executable(test_ble_app_inbox
  test_ble_app_inbox.cpp
  rtt_stub.cpp
  "${SLATE_ROOT}/src/sdp_frame.cpp"
  "${SLATE_ROOT}/src/sdp_diag.cpp"
  "${SLATE_ROOT}/src/ble_link.cpp"
)
target_include_directories(test_ble_app_inbox PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_ble_app_inbox PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_ble_app_inbox ble_app_inbox)

add_executable(test_ble_gap_adv_policy
  test_ble_gap_adv_policy.cpp
)
target_include_directories(test_ble_gap_adv_policy PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_ble_gap_adv_policy PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_ble_gap_adv_policy ble_gap_adv_policy)

add_executable(test_sdp_diag
  test_sdp_diag.cpp
  "${SLATE_ROOT}/src/sdp_frame.cpp"
  "${SLATE_ROOT}/src/sdp_diag.cpp"
  "${SLATE_ROOT}/src/ble_link.cpp"
  "${SLATE_ROOT}/src/ble_mbuf_stats.cpp"
)
target_include_directories(test_sdp_diag PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_sdp_diag PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_sdp_diag sdp_diag)

add_executable(test_session
  test_session.cpp
  "${SLATE_ROOT}/src/session.cpp"
)
target_include_directories(test_session PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_session PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_session session)

add_executable(test_local_core
  test_local_core.cpp
  "${SLATE_ROOT}/src/persist.cpp"
  "${SLATE_ROOT}/src/wall_clock.cpp"
  "${SLATE_ROOT}/src/notif_store.cpp"
  "${SLATE_ROOT}/src/alarm_sched.cpp"
  "${SLATE_ROOT}/src/battery.cpp"
  "${SLATE_ROOT}/src/local_ui.cpp"
  "${SLATE_ROOT}/src/local_core.cpp"
  "${SLATE_ROOT}/src/bma42x.cpp"
)
target_include_directories(test_local_core PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_local_core PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_local_core local_core)

add_executable(test_battery
  test_battery.cpp
  "${SLATE_ROOT}/src/battery.cpp"
)
target_include_directories(test_battery PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_battery PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_battery battery)

add_executable(test_local_ui
  test_local_ui.cpp
  "${SLATE_ROOT}/src/local_ui.cpp"
  "${SLATE_ROOT}/src/notif_store.cpp"
  "${SLATE_ROOT}/src/wall_clock.cpp"
  "${SLATE_ROOT}/src/persist.cpp"
  "${SLATE_ROOT}/src/sdp_parser.cpp"
  "${SLATE_ROOT}/src/sdp_validate.cpp"
)
target_include_directories(test_local_ui PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_local_ui PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_local_ui local_ui)

add_executable(test_assets
  test_assets.cpp
  "${SLATE_ROOT}/src/asset_pack.cpp"
  "${SLATE_ROOT}/src/glyph_cache.cpp"
  "${SLATE_ROOT}/src/asset_xfer.cpp"
)
target_include_directories(test_assets PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_assets PRIVATE -Wall -Wextra -Wpedantic)
# Run from repo root so shared/generated/core.slap resolves.
add_test(NAME assets COMMAND test_assets)
set_tests_properties(assets PROPERTIES
  WORKING_DIRECTORY "${SLATE_ROOT}")
if(WIN32 AND DEFINED SLATE_HOST_TEST_PATH_ESC)
  set_tests_properties(assets PROPERTIES
    ENVIRONMENT "PATH=${SLATE_HOST_TEST_PATH_ESC}")
endif()
if(TARGET slate_host_runtime_dlls)
  add_dependencies(test_assets slate_host_runtime_dlls)
endif()

add_executable(test_ota_xfer
  test_ota_xfer.cpp
  "${SLATE_ROOT}/src/ota_xfer.cpp"
  "${SLATE_ROOT}/src/dfu_nordic.cpp"
  "${SLATE_ROOT}/src/sha256.cpp"
)
target_include_directories(test_ota_xfer PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_ota_xfer PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_ota_xfer ota_xfer)

# WDT withhold-on-hold (InfiniTime SystemTask). Compiles real src/wdt.cpp with
# host-mapped RR0 and a fake board::button_raw().
add_executable(test_wdt_hold
  test_wdt_hold.cpp
  wdt_host_fakes.cpp
  "${SLATE_ROOT}/src/wdt.cpp"
)
target_include_directories(test_wdt_hold PRIVATE "${SLATE_ROOT}/include")
target_compile_definitions(test_wdt_hold PRIVATE SLATE_HOST_MMIO=1)
target_compile_options(test_wdt_hold PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_wdt_hold wdt_hold)

# App-task-only WDT pets (no tick/idle reload when wedged).
add_executable(test_wdt_app_pet
  test_wdt_app_pet.cpp
  wdt_host_fakes.cpp
  "${SLATE_ROOT}/src/wdt.cpp"
)
target_include_directories(test_wdt_app_pet PRIVATE "${SLATE_ROOT}/include")
target_compile_definitions(test_wdt_app_pet PRIVATE SLATE_HOST_MMIO=1)
target_compile_options(test_wdt_app_pet PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_wdt_app_pet wdt_app_pet)

# Vibration pattern sequencer (InfiniTime MotorController port). Header-only
# state machine, so no FreeRTOS fakes are needed.
add_executable(test_motor
  test_motor.cpp
)
target_include_directories(test_motor PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_motor PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_motor motor)

add_executable(test_spi_timeout
  test_spi_timeout.cpp
)
target_include_directories(test_spi_timeout PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_spi_timeout PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_spi_timeout spi_timeout)
