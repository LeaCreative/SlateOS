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
)
target_include_directories(test_ble_link PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_ble_link PRIVATE -Wall -Wextra -Wpedantic)
slate_host_test(test_ble_link ble_link)

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
