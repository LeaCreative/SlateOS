cmake_minimum_required(VERSION 3.21)

# libFuzzer harness for the SDP display-list parser.
# Requires clang with libFuzzer (Linux/macOS, or Windows clang+lld).
#
#   cmake -S tests/fuzz -B build/fuzz -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build/fuzz
#   ./build/fuzz/sdp_parser_fuzz -max_len=4096 -timeout=2 \
#       ${CMAKE_CURRENT_SOURCE_DIR}/corpus

project(slate_sdp_fuzz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "SDP fuzzer requires Clang (libFuzzer). Got ${CMAKE_CXX_COMPILER_ID}")
endif()

set(SLATE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(sdp_parser_fuzz
  sdp_parser_fuzz.cpp
  "${SLATE_ROOT}/src/sdp_validate.cpp"
  "${SLATE_ROOT}/src/sdp_parser.cpp"
)

target_include_directories(sdp_parser_fuzz PRIVATE
  "${SLATE_ROOT}/include"
)

target_compile_options(sdp_parser_fuzz PRIVATE
  -Wall -Wextra -Wpedantic
  -fsanitize=fuzzer,address,undefined
  -fno-omit-frame-pointer
)

target_link_options(sdp_parser_fuzz PRIVATE
  -fsanitize=fuzzer,address,undefined
)

add_executable(asset_pack_fuzz
  asset_pack_fuzz.cpp
  "${SLATE_ROOT}/src/asset_pack.cpp"
)

target_include_directories(asset_pack_fuzz PRIVATE
  "${SLATE_ROOT}/include"
)

target_compile_options(asset_pack_fuzz PRIVATE
  -Wall -Wextra -Wpedantic
  -fsanitize=fuzzer,address,undefined
  -fno-omit-frame-pointer
)

target_link_options(asset_pack_fuzz PRIVATE
  -fsanitize=fuzzer,address,undefined
)
