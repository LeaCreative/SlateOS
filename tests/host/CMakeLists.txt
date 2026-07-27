cmake_minimum_required(VERSION 3.21)

# Host-side unit tests — configure WITHOUT the ARM toolchain:
#   cmake -S tests/host -B build/host-tests
#   cmake --build build/host-tests
#   ctest --test-dir build/host-tests

project(slate_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(SLATE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(test_hit_test
  test_hit_test.cpp
)
target_include_directories(test_hit_test PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_hit_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME hit_test COMMAND test_hit_test)

add_executable(test_sdp_parser
  test_sdp_parser.cpp
  "${SLATE_ROOT}/src/sdp_validate.cpp"
  "${SLATE_ROOT}/src/sdp_parser.cpp"
)
target_include_directories(test_sdp_parser PRIVATE "${SLATE_ROOT}/include")
target_compile_options(test_sdp_parser PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME sdp_parser COMMAND test_sdp_parser)
