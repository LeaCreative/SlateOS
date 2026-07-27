#pragma once

#include "sdp_opcodes.hpp"

#include <cstddef>
#include <cstdint>

namespace sdp {

enum class Status : std::uint8_t {
  Ok = 0,
  Truncated,
  Reject,
};

struct Style {
  std::uint8_t mode = style::ModeFill;   // 0 fill, 1 stroke, 2 fill+stroke
  std::uint8_t width = 1u;               // 1–15 (0 encoded → 1)
};

struct Palette {
  std::uint16_t entry[kPaletteSize] = {};
  bool set[kPaletteSize] = {};
};

// Bounds-checked cursor over an untrusted buffer.  Never reads past size.
class Reader {
public:
  Reader(const std::uint8_t* data, std::size_t size)
      : data_(data), size_(size), pos_(0), status_(Status::Ok) {}

  Status status() const { return status_; }
  void set_reject() { if (status_ == Status::Ok) status_ = Status::Reject; }
  void set_truncated() { if (status_ == Status::Ok) status_ = Status::Truncated; }
  bool ok() const { return status_ == Status::Ok; }

  std::size_t pos() const { return pos_; }
  std::size_t remaining() const {
    return (pos_ <= size_) ? (size_ - pos_) : 0u;
  }

  bool need(std::size_t n) {
    if (!ok()) return false;
    if (remaining() < n) {
      set_truncated();
      return false;
    }
    return true;
  }

  std::uint8_t take_u8() {
    if (!need(1u)) return 0u;
    return data_[pos_++];
  }

  std::uint16_t take_u16_le() {
    if (!need(2u)) return 0u;
    const std::uint16_t lo = data_[pos_++];
    const std::uint16_t hi = data_[pos_++];
    return static_cast<std::uint16_t>(lo | (hi << 8u));
  }

  const std::uint8_t* take_bytes(std::size_t n) {
    if (!need(n)) return nullptr;
    const std::uint8_t* p = data_ + pos_;
    pos_ += n;
    return p;
  }

  void skip(std::size_t n) {
    if (!need(n)) return;
    pos_ += n;
  }

private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_;
  Status status_;
};

// Shared validation helpers — use these in every opcode handler.

inline bool check_coord(std::uint8_t v) {
  return v < kDisplaySize;
}

// Rectangle origin + size must stay within [0, 240).
inline bool check_rect(std::uint8_t x, std::uint8_t y,
                       std::uint8_t w, std::uint8_t h) {
  if (!check_coord(x) || !check_coord(y)) return false;
  if (w == 0u || h == 0u) return false;
  if (static_cast<std::uint16_t>(x) + w > kDisplaySize) return false;
  if (static_cast<std::uint16_t>(y) + h > kDisplaySize) return false;
  return true;
}

inline bool check_enum(std::uint8_t value, std::uint8_t max_inclusive) {
  return value <= max_inclusive;
}

inline bool check_flags(std::uint8_t value, std::uint8_t allowed_mask) {
  return (value & static_cast<std::uint8_t>(~allowed_mask)) == 0u;
}

inline bool check_id_u8(std::uint8_t id, std::uint8_t max_id) {
  return id <= max_id;
}

inline bool check_id_u16(std::uint16_t id, std::uint16_t max_id) {
  return id <= max_id;
}

// Decode COLOR (§4.3).  Variable length: 1 or 3 bytes.
Status decode_color(Reader& r, const Palette& pal, std::uint16_t* out_rgb565);

// Decode STYLE (§4.3).  Rejects mode 3 and reserved bits 6–7.
Status decode_style(Reader& r, Style* out);

}  // namespace sdp
