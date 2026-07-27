#pragma once

#include <cstdint>

// Slate GATT UUIDs — §4.1
//
// Base (uuidgen): e979acfb-c338-44fa-a962-e96e4cf078f3
// Discriminant in time_mid (bytes 4–5 / third UUID group):
//   Service  …-0000-…
//   RX       …-0001-…  Write Without Response
//   TX       …-0002-…  Notify
//   STATUS   …-0003-…  Read, Notify

namespace slate {
namespace uuid {

// Base with time_mid = 0x0000 (service). Little-endian byte order for NimBLE.
constexpr std::uint8_t kService[16] = {
    0xfb, 0xac, 0x79, 0xe9,  // time_low
    0x00, 0x00,              // time_mid = 0000
    0xfa, 0x44,              // time_hi_and_version
    0xa9, 0x62,              // clock_seq
    0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3  // node
};

constexpr std::uint8_t kRx[16] = {
    0xfb, 0xac, 0x79, 0xe9,
    0x01, 0x00,  // 0001
    0xfa, 0x44,
    0xa9, 0x62,
    0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3
};

constexpr std::uint8_t kTx[16] = {
    0xfb, 0xac, 0x79, 0xe9,
    0x02, 0x00,  // 0002
    0xfa, 0x44,
    0xa9, 0x62,
    0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3
};

constexpr std::uint8_t kStatus[16] = {
    0xfb, 0xac, 0x79, 0xe9,
    0x03, 0x00,  // 0003
    0xfa, 0x44,
    0xa9, 0x62,
    0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3
};

constexpr const char* kBaseString =
    "e979acfb-c338-44fa-a962-e96e4cf078f3";
constexpr const char* kServiceString =
    "e979acfb-c338-0000-a962-e96e4cf078f3";
constexpr const char* kRxString =
    "e979acfb-c338-0001-a962-e96e4cf078f3";
constexpr const char* kTxString =
    "e979acfb-c338-0002-a962-e96e4cf078f3";
constexpr const char* kStatusString =
    "e979acfb-c338-0003-a962-e96e4cf078f3";

}  // namespace uuid
}  // namespace slate
