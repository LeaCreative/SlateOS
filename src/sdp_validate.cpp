#include "sdp_validate.hpp"

namespace sdp {

Status decode_color(Reader& r, const Palette& pal, std::uint16_t* out_rgb565) {
  if (!r.ok() || out_rgb565 == nullptr) {
    return Status::Reject;
  }

  const std::uint8_t tag = r.take_u8();
  if (!r.ok()) {
    return r.status();
  }

  if (tag == color_tag::LiteralRgb565) {
    const std::uint16_t rgb = r.take_u16_le();
    if (!r.ok()) {
      return r.status();
    }
    *out_rgb565 = rgb;
    return Status::Ok;
  }

  if (tag >= color_tag::PaletteMin && tag <= color_tag::PaletteMax) {
    const std::uint8_t idx = static_cast<std::uint8_t>(tag - 1u);
    if (!pal.set[idx]) {
      r.set_reject();
      return Status::Reject;
    }
    *out_rgb565 = pal.entry[idx];
    return Status::Ok;
  }

  // 0x11–0xFF reserved.
  r.set_reject();
  return Status::Reject;
}

Status decode_style(Reader& r, Style* out) {
  if (!r.ok() || out == nullptr) {
    return Status::Reject;
  }

  const std::uint8_t raw = r.take_u8();
  if (!r.ok()) {
    return r.status();
  }

  if ((raw & style::ReservedMask) != 0u) {
    r.set_reject();
    return Status::Reject;
  }

  const std::uint8_t mode = static_cast<std::uint8_t>(raw & style::ModeMask);
  if (mode == style::ModeReserved) {
    r.set_reject();
    return Status::Reject;
  }

  std::uint8_t width =
      static_cast<std::uint8_t>((raw >> style::WidthShift) & style::WidthMask);
  if (width == 0u) {
    width = 1u;
  }

  out->mode = mode;
  out->width = width;
  return Status::Ok;
}

}  // namespace sdp
