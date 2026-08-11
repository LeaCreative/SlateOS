#pragma once

#include "sdp_opcodes.hpp"

#include <cstddef>
#include <cstdint>

// Encode §4.4 input events into a small buffer (channel 2 payload).

namespace sdp {
namespace input_wire {

inline void put_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

inline std::size_t encode_tap(std::uint8_t* out, std::size_t cap,
                              std::uint16_t elem_id, std::uint8_t x,
                              std::uint8_t y) {
  if (cap < 5u) return 0u;
  out[0] = input_op::TAP;
  put_u16(out + 1, elem_id);
  out[3] = x;
  out[4] = y;
  return 5u;
}

inline std::size_t encode_long_press(std::uint8_t* out, std::size_t cap,
                                     std::uint16_t elem_id) {
  if (cap < 3u) return 0u;
  out[0] = input_op::LONG_PRESS;
  put_u16(out + 1, elem_id);
  return 3u;
}

inline std::size_t encode_swipe(std::uint8_t* out, std::size_t cap,
                                std::uint8_t dir) {
  if (cap < 2u) return 0u;
  out[0] = input_op::SWIPE;
  out[1] = dir;
  return 2u;
}

inline std::size_t encode_button(std::uint8_t* out, std::size_t cap,
                                 std::uint8_t action) {
  if (cap < 2u) return 0u;
  out[0] = input_op::BUTTON;
  out[1] = action;
  return 2u;
}

inline std::size_t encode_scroll_pos(std::uint8_t* out, std::size_t cap,
                                     std::uint8_t region,
                                     std::uint16_t offset) {
  if (cap < 4u) return 0u;
  out[0] = input_op::SCROLL_POS;
  out[1] = region;
  put_u16(out + 2, offset);
  return 4u;
}

inline std::size_t encode_back(std::uint8_t* out, std::size_t cap) {
  if (cap < 1u) return 0u;
  out[0] = input_op::BACK;
  return 1u;
}

inline std::size_t encode_session_end(std::uint8_t* out, std::size_t cap,
                                      std::uint8_t reason) {
  if (cap < 2u) return 0u;
  out[0] = input_op::SESSION_END;
  out[1] = reason;
  return 2u;
}

inline std::size_t encode_multi_tap(std::uint8_t* out, std::size_t cap,
                                    std::uint8_t count, std::uint16_t elem_id,
                                    std::uint8_t x, std::uint8_t y) {
  if (cap < 6u) return 0u;
  out[0] = input_op::MULTI_TAP;
  out[1] = count;
  put_u16(out + 2, elem_id);
  out[4] = x;
  out[5] = y;
  return 6u;
}

inline std::size_t encode_edge_swipe(std::uint8_t* out, std::size_t cap,
                                     std::uint8_t edge_v, std::uint8_t dir,
                                     std::uint8_t distance) {
  if (cap < 4u) return 0u;
  out[0] = input_op::EDGE_SWIPE;
  out[1] = edge_v;
  out[2] = dir;
  out[3] = distance;
  return 4u;
}

inline std::size_t encode_touch_down(std::uint8_t* out, std::size_t cap,
                                     std::uint16_t elem_id, std::uint8_t x,
                                     std::uint8_t y) {
  if (cap < 5u) return 0u;
  out[0] = input_op::TOUCH_DOWN;
  put_u16(out + 1, elem_id);
  out[3] = x;
  out[4] = y;
  return 5u;
}

inline std::size_t encode_touch_up(std::uint8_t* out, std::size_t cap,
                                   std::uint16_t elem_id, std::uint8_t x,
                                   std::uint8_t y, std::uint16_t duration_ms) {
  if (cap < 7u) return 0u;
  out[0] = input_op::TOUCH_UP;
  put_u16(out + 1, elem_id);
  out[3] = x;
  out[4] = y;
  put_u16(out + 5, duration_ms);
  return 7u;
}

/** Watch → phone: request notification body for key. */
inline std::size_t encode_notif_req(std::uint8_t* out, std::size_t cap,
                                    const char* key, std::uint8_t key_len) {
  if (out == nullptr || key == nullptr) {
    return 0u;
  }
  if (cap < static_cast<std::size_t>(2u + key_len)) {
    return 0u;
  }
  out[0] = input_op::NOTIF_REQ;
  out[1] = key_len;
  for (std::uint8_t i = 0u; i < key_len; ++i) {
    out[2u + i] = static_cast<std::uint8_t>(key[i]);
  }
  return static_cast<std::size_t>(2u + key_len);
}

}  // namespace input_wire
}  // namespace sdp
