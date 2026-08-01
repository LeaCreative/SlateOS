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

// NimBLE stores a 128-bit UUID as the text form reversed **in full** — byte 0
// is the LAST byte of the string. These arrays previously reversed only within
// each group (the Microsoft GUID mixed-endian convention), so the watch
// published `f378f04c-6ee9-62a9-44fa-0000e979acfb` and no central looking for
// the real UUID could match it (N-17). Verified against hardware with
// nRF Connect. `test_uuids` now pins each array to its string.
constexpr std::uint8_t kService[16] = {
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9,
    0x00, 0x00,  // third text group = 0000 (service), little-endian
    0x38, 0xc3, 0xfb, 0xac, 0x79, 0xe9
};

constexpr std::uint8_t kRx[16] = {
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9,
    0x01, 0x00,  // 0001
    0x38, 0xc3, 0xfb, 0xac, 0x79, 0xe9
};

constexpr std::uint8_t kTx[16] = {
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9,
    0x02, 0x00,  // 0002
    0x38, 0xc3, 0xfb, 0xac, 0x79, 0xe9
};

constexpr std::uint8_t kStatus[16] = {
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9,
    0x03, 0x00,  // 0003
    0x38, 0xc3, 0xfb, 0xac, 0x79, 0xe9
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
