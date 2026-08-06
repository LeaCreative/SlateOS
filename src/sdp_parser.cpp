#include "sdp_parser.hpp"

namespace sdp {
namespace {

void reject(Reader& r, ParseResult* out) {
  r.set_reject();
  out->status = Status::Reject;
}

void finish_reader(Reader& r, ParseResult* out) {
  if (out->status == Status::Ok) {
    out->status = r.status();
  }
  out->bytes_consumed = r.pos();
}

bool require_exec(const ParseOptions& opt, Sink* sink) {
  return opt.execute && sink != nullptr;
}

}  // namespace

ParseResult parse(const std::uint8_t* data, std::size_t size,
                  const ParseOptions& opt, Sink* sink) {
  ParseResult out{};

  if (data == nullptr && size != 0u) {
    out.status = Status::Reject;
    return out;
  }
  if (size > opt.max_bytes) {
    out.status = Status::Reject;
    return out;
  }

  Reader r(data, size);
  Palette pal{};
  std::uint32_t elem_depth = 0u;
  bool committed = false;

  // Vertical bound for drawing ops.
  //
  // Outside a scroll region this is the display: nothing may be addressed off
  // screen. Inside one it becomes the declared content height, because content
  // taller than the viewport is the entire point of scrolling — and until this
  // existed, check_rect() bounded every op to 240 px, so a scrollable list was
  // impossible to express and SCROLL_REGION had never once been usable for its
  // purpose. The renderer is already safe either way: map_y() clamps to 0..239
  // and the region installs a clip.
  //
  // x stays bounded to the display; there is no horizontal scrolling.
  std::uint16_t y_limit = kDisplaySize;
  const auto ok_y = [&y_limit](std::uint8_t v) {
    return static_cast<std::uint16_t>(v) < y_limit;
  };
  const auto ok_rect = [&y_limit](std::uint8_t x, std::uint8_t y,
                                  std::uint8_t w, std::uint8_t h) {
    if (!check_coord(x)) return false;
    if (static_cast<std::uint16_t>(y) >= y_limit) return false;
    if (w == 0u || h == 0u) return false;
    if (static_cast<std::uint16_t>(x) + w > kDisplaySize) return false;
    if (static_cast<std::uint16_t>(y) + h > y_limit) return false;
    return true;
  };

  while (r.ok() && r.remaining() > 0u) {
    if (committed) {
      // Trailing bytes after COMMIT are rejected.
      reject(r, &out);
      finish_reader(r, &out);
      return out;
    }

    if (out.ops_consumed >= opt.max_ops) {
      reject(r, &out);
      finish_reader(r, &out);
      return out;
    }

    const std::uint8_t opcode = r.take_u8();
    if (!r.ok()) {
      break;
    }
    ++out.ops_consumed;

    // Extension range: length-prefixed (§7.2). Known extensions are decoded;
    // anything else — including fields a newer revision appends to an extension
    // we already know — is skipped using the declared length.
    if (opcode >= op::EXT_MIN && opcode <= op::EXT_MAX) {
      const std::uint16_t len = r.take_u16_le();
      if (!r.ok()) {
        break;
      }
      // A payload running past the buffer is a truncated list, not a malformed
      // one, so let the reader report Truncated and resync upstream.
      if (!r.need(len)) {
        break;
      }
      const std::size_t before = r.remaining();
      if (opcode == op::TEXT_SCALED) {
        const std::uint8_t font = r.take_u8();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        if (!r.ok()) break;
        std::uint16_t color = 0u;
        if (decode_color(r, pal, &color) != Status::Ok) break;
        const std::uint8_t al = r.take_u8();
        const std::uint8_t scale = r.take_u8();
        const std::uint8_t text_len = r.take_u8();
        if (!r.ok()) break;
        if (!check_id_u8(font, kMaxFontId) || !check_coord(x) ||
            !ok_y(y) || !check_enum(al, align::Max) ||
            scale < kMinTextScale || scale > kMaxTextScale) {
          reject(r, &out);
          break;
        }
        const std::uint8_t* utf8 = r.take_bytes(text_len);
        if (!r.ok()) break;
        if (require_exec(opt, sink)) {
          sink->text_scaled(font, x, y, color, al, scale, text_len, utf8);
        }
      }
      const std::size_t used = before - r.remaining();
      if (used > len) {
        reject(r, &out);
        break;
      }
      r.skip(len - used);
      if (!r.ok()) {
        break;
      }
      continue;
    }

    switch (opcode) {
      case op::CLEAR: {
        std::uint16_t color = 0u;
        if (decode_color(r, pal, &color) != Status::Ok) break;
        if (require_exec(opt, sink)) sink->clear(color);
        break;
      }

      case op::SET_PALETTE: {
        const std::uint8_t idx = r.take_u8();
        const std::uint16_t rgb = r.take_u16_le();
        if (!r.ok()) break;
        if (idx >= kPaletteSize) {
          reject(r, &out);
          break;
        }
        pal.entry[idx] = rgb;
        pal.set[idx] = true;
        if (require_exec(opt, sink)) sink->set_palette(idx, rgb);
        break;
      }

      case op::RECT: {
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        const std::uint8_t w = r.take_u8();
        const std::uint8_t h = r.take_u8();
        if (!r.ok()) break;
        if (!ok_rect(x, y, w, h)) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        Style st{};
        if (decode_color(r, pal, &color) != Status::Ok) break;
        if (decode_style(r, &st) != Status::Ok) break;
        if (require_exec(opt, sink)) sink->rect(x, y, w, h, color, st);
        break;
      }

      case op::RECT_ROUND: {
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        const std::uint8_t w = r.take_u8();
        const std::uint8_t h = r.take_u8();
        const std::uint8_t rad = r.take_u8();
        if (!r.ok()) break;
        if (!ok_rect(x, y, w, h)) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        Style st{};
        if (decode_color(r, pal, &color) != Status::Ok) break;
        if (decode_style(r, &st) != Status::Ok) break;
        if (require_exec(opt, sink)) {
          sink->rect_round(x, y, w, h, rad, color, st);
        }
        break;
      }

      case op::LINE: {
        const std::uint8_t x0 = r.take_u8();
        const std::uint8_t y0 = r.take_u8();
        const std::uint8_t x1 = r.take_u8();
        const std::uint8_t y1 = r.take_u8();
        if (!r.ok()) break;
        if (!check_coord(x0) || !ok_y(y0) ||
            !check_coord(x1) || !ok_y(y1)) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        if (decode_color(r, pal, &color) != Status::Ok) break;
        const std::uint8_t width = r.take_u8();
        if (!r.ok()) break;
        if (require_exec(opt, sink)) sink->line(x0, y0, x1, y1, color, width);
        break;
      }

      case op::CIRCLE: {
        const std::uint8_t cx = r.take_u8();
        const std::uint8_t cy = r.take_u8();
        const std::uint8_t rad = r.take_u8();
        if (!r.ok()) break;
        if (!check_coord(cx) || !ok_y(cy)) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        Style st{};
        if (decode_color(r, pal, &color) != Status::Ok) break;
        if (decode_style(r, &st) != Status::Ok) break;
        if (require_exec(opt, sink)) sink->circle(cx, cy, rad, color, st);
        break;
      }

      case op::ARC: {
        const std::uint8_t cx = r.take_u8();
        const std::uint8_t cy = r.take_u8();
        const std::uint8_t rad = r.take_u8();
        const std::uint16_t a0 = r.take_u16_le();
        const std::uint16_t a1 = r.take_u16_le();
        if (!r.ok()) break;
        if (!check_coord(cx) || !ok_y(cy) || a0 >= 360u || a1 >= 360u) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        if (decode_color(r, pal, &color) != Status::Ok) break;
        const std::uint8_t width = r.take_u8();
        if (!r.ok()) break;
        if (require_exec(opt, sink)) {
          sink->arc(cx, cy, rad, a0, a1, color, width);
        }
        break;
      }

      case op::POLYLINE: {
        const std::uint8_t count = r.take_u8();
        if (!r.ok()) break;
        if (count < 2u) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        if (decode_color(r, pal, &color) != Status::Ok) break;
        const std::uint8_t width = r.take_u8();
        if (!r.ok()) break;
        const std::size_t pts_bytes =
            static_cast<std::size_t>(count) * 2u;
        const std::uint8_t* pts = r.take_bytes(pts_bytes);
        if (!r.ok()) break;
        for (std::uint8_t i = 0u; i < count; ++i) {
          if (!check_coord(pts[i * 2u]) || !ok_y(pts[i * 2u + 1u])) {
            reject(r, &out);
            break;
          }
        }
        if (!r.ok() || out.status == Status::Reject) break;
        if (require_exec(opt, sink)) {
          sink->polyline(count, color, width, pts);
        }
        break;
      }

      case op::CLIP_RECT: {
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        const std::uint8_t w = r.take_u8();
        const std::uint8_t h = r.take_u8();
        if (!r.ok()) break;
        if (!ok_rect(x, y, w, h)) {
          reject(r, &out);
          break;
        }
        if (require_exec(opt, sink)) sink->clip_rect(x, y, w, h);
        break;
      }

      case op::CLIP_CLEAR: {
        // Ends the scroll region too (see Sink::clip_clear).
        y_limit = kDisplaySize;
        if (require_exec(opt, sink)) sink->clip_clear();
        break;
      }

      case op::TEXT: {
        const std::uint8_t font = r.take_u8();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        if (!r.ok()) break;
        if (!check_id_u8(font, kMaxFontId) || !check_coord(x) ||
            !ok_y(y)) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        if (decode_color(r, pal, &color) != Status::Ok) break;
        const std::uint8_t al = r.take_u8();
        const std::uint8_t len = r.take_u8();
        if (!r.ok()) break;
        if (!check_enum(al, align::Max)) {
          reject(r, &out);
          break;
        }
        const std::uint8_t* utf8 = r.take_bytes(len);
        if (!r.ok()) break;
        if (require_exec(opt, sink)) {
          sink->text(font, x, y, color, al, len, utf8);
        }
        break;
      }

      case op::TEXT_BOX: {
        const std::uint8_t font = r.take_u8();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        const std::uint8_t w = r.take_u8();
        const std::uint8_t h = r.take_u8();
        if (!r.ok()) break;
        if (!check_id_u8(font, kMaxFontId) || !ok_rect(x, y, w, h)) {
          reject(r, &out);
          break;
        }
        std::uint16_t color = 0u;
        if (decode_color(r, pal, &color) != Status::Ok) break;
        const std::uint8_t al = r.take_u8();
        const std::uint8_t flags = r.take_u8();
        const std::uint8_t len = r.take_u8();
        if (!r.ok()) break;
        if (!check_enum(al, align::Max) ||
            !check_flags(flags, text_box_flags::Allowed)) {
          reject(r, &out);
          break;
        }
        const std::uint8_t* utf8 = r.take_bytes(len);
        if (!r.ok()) break;
        if (require_exec(opt, sink)) {
          sink->text_box(font, x, y, w, h, color, al, flags, len, utf8);
        }
        break;
      }

      case op::ICON: {
        const std::uint8_t atlas = r.take_u8();
        const std::uint16_t id = r.take_u16_le();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        if (!r.ok()) break;
        if (!check_id_u8(atlas, kMaxAtlasId) ||
            !check_id_u16(id, kMaxIconId) ||
            !check_coord(x) || !ok_y(y)) {
          reject(r, &out);
          break;
        }
        std::uint16_t tint = 0u;
        if (decode_color(r, pal, &tint) != Status::Ok) break;
        if (require_exec(opt, sink)) sink->icon(atlas, id, x, y, tint);
        break;
      }

      case op::IMAGE: {
        const std::uint8_t asset = r.take_u8();
        const std::uint16_t id = r.take_u16_le();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        if (!r.ok()) break;
        if (!check_id_u8(asset, kMaxAssetId) ||
            !check_id_u16(id, kMaxImageId) ||
            !check_coord(x) || !ok_y(y)) {
          reject(r, &out);
          break;
        }
        if (require_exec(opt, sink)) sink->image(asset, id, x, y);
        break;
      }

      case op::PROGRESS_BAR: {
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        const std::uint8_t w = r.take_u8();
        const std::uint8_t h = r.take_u8();
        const std::uint8_t pct = r.take_u8();
        if (!r.ok()) break;
        if (!ok_rect(x, y, w, h) || pct > 100u) {
          reject(r, &out);
          break;
        }
        std::uint16_t fg = 0u;
        std::uint16_t bg = 0u;
        if (decode_color(r, pal, &fg) != Status::Ok) break;
        if (decode_color(r, pal, &bg) != Status::Ok) break;
        if (require_exec(opt, sink)) {
          sink->progress_bar(x, y, w, h, pct, fg, bg);
        }
        break;
      }

      case op::PROGRESS_ARC: {
        const std::uint8_t cx = r.take_u8();
        const std::uint8_t cy = r.take_u8();
        const std::uint8_t rad = r.take_u8();
        const std::uint8_t pct = r.take_u8();
        if (!r.ok()) break;
        if (!check_coord(cx) || !ok_y(cy) || pct > 100u) {
          reject(r, &out);
          break;
        }
        std::uint16_t fg = 0u;
        std::uint16_t bg = 0u;
        if (decode_color(r, pal, &fg) != Status::Ok) break;
        if (decode_color(r, pal, &bg) != Status::Ok) break;
        const std::uint8_t width = r.take_u8();
        if (!r.ok()) break;
        if (require_exec(opt, sink)) {
          sink->progress_arc(cx, cy, rad, pct, fg, bg, width);
        }
        break;
      }

      case op::BEGIN_ELEM: {
        const std::uint16_t id = r.take_u16_le();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        const std::uint8_t w = r.take_u8();
        const std::uint8_t h = r.take_u8();
        const std::uint8_t flags = r.take_u8();
        if (!r.ok()) break;
        if (!ok_rect(x, y, w, h) ||
            !check_flags(flags, elem_flags::Allowed)) {
          reject(r, &out);
          break;
        }
        if (elem_depth >= kMaxElemDepth) {
          reject(r, &out);
          break;
        }
        ++elem_depth;
        if (require_exec(opt, sink)) {
          sink->begin_elem(id, x, y, w, h, flags);
        }
        break;
      }

      case op::END_ELEM: {
        if (elem_depth == 0u) {
          reject(r, &out);
          break;
        }
        --elem_depth;
        if (require_exec(opt, sink)) sink->end_elem();
        break;
      }

      case op::SCROLL_REGION: {
        const std::uint8_t y = r.take_u8();
        const std::uint8_t h = r.take_u8();
        const std::uint16_t content_h = r.take_u16_le();
        if (!r.ok()) break;
        if (!check_coord(y) || h == 0u ||
            static_cast<std::uint16_t>(y) + h > kDisplaySize) {
          reject(r, &out);
          break;
        }
        // Only ever relaxes, never tightens: a list that was valid before
        // this existed stays valid.
        if (content_h > y_limit) {
          y_limit = content_h;
        }
        if (require_exec(opt, sink)) {
          sink->scroll_region(y, h, content_h);
        }
        break;
      }

      case op::PATCH: {
        const std::uint8_t slot = r.take_u8();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        const std::uint8_t w = r.take_u8();
        const std::uint8_t h = r.take_u8();
        const std::uint8_t format = r.take_u8();
        const std::uint8_t encoding = r.take_u8();
        const std::uint16_t len = r.take_u16_le();
        if (!r.ok()) break;
        if (!ok_rect(x, y, w, h) ||
            !check_enum(format, patch_format::Max) ||
            !check_enum(encoding, patch_encoding::Max) ||
            len > 4096u) {
          reject(r, &out);
          break;
        }
        const std::uint8_t* pdata = r.take_bytes(len);
        if (!r.ok()) break;
        if (require_exec(opt, sink)) {
          sink->patch(slot, x, y, w, h, format, encoding, len, pdata);
        }
        break;
      }

      case op::PATCH_REF: {
        const std::uint8_t slot = r.take_u8();
        const std::uint8_t x = r.take_u8();
        const std::uint8_t y = r.take_u8();
        if (!r.ok()) break;
        if (!check_coord(x) || !ok_y(y)) {
          reject(r, &out);
          break;
        }
        if (require_exec(opt, sink)) sink->patch_ref(slot, x, y);
        break;
      }

      case op::HAPTIC: {
        const std::uint8_t pattern = r.take_u8();
        if (!r.ok()) break;
        if (!check_enum(pattern, haptic_pattern::Max)) {
          reject(r, &out);
          break;
        }
        if (require_exec(opt, sink)) sink->haptic(pattern);
        break;
      }

      case op::BACKLIGHT: {
        const std::uint8_t level = r.take_u8();
        if (!r.ok()) break;
        if (level > 100u) {
          reject(r, &out);
          break;
        }
        if (require_exec(opt, sink)) sink->backlight(level);
        break;
      }

      case op::COMMIT: {
        const std::uint8_t flags = r.take_u8();
        if (!r.ok()) break;
        if (!check_flags(flags, commit_flags::Allowed)) {
          reject(r, &out);
          break;
        }
        if (elem_depth != 0u) {
          reject(r, &out);
          break;
        }
        out.saw_commit = true;
        out.commit_flags = flags;
        committed = true;
        if (require_exec(opt, sink)) sink->commit(flags);
        break;
      }

      case op::RETAIN: {
        const std::uint16_t ttl = r.take_u16_le();
        if (!r.ok()) break;
        out.saw_retain = true;
        out.retain_ttl_s = ttl;
        if (require_exec(opt, sink)) sink->retain(ttl);
        break;
      }

      default:
        // Unknown base opcode — reject (frozen set is not skippable).
        reject(r, &out);
        break;
    }

    if (out.status == Status::Reject || !r.ok()) {
      break;
    }
  }

  finish_reader(r, &out);
  return out;
}

}  // namespace sdp
