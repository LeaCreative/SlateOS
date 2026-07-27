#include "asset_pack.hpp"

#include <cstring>

namespace slate {
namespace pack {
namespace {

std::uint16_t rd_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t rd_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t len) {
  std::uint32_t h = 2166136261u;
  for (std::size_t i = 0u; i < len; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

Status parse(const std::uint8_t* data, std::size_t len, Index* out) {
  if (out == nullptr || data == nullptr) {
    return Status::Truncated;
  }
  std::memset(out, 0, sizeof(*out));
  if (len < kHeaderBytes) {
    return Status::Truncated;
  }
  if (std::memcmp(data, kMagic, 4) != 0) {
    return Status::BadMagic;
  }
  const std::uint16_t ver = rd_u16(data + 4);
  if (ver != kFormatVersion) {
    return Status::BadVersion;
  }
  out->version = ver;
  // flags at +6 ignored for now
  out->content_hash = rd_u32(data + 8);
  const std::uint32_t index_off = rd_u32(data + 12);
  const std::uint32_t index_len = rd_u32(data + 16);

  if (index_off < kHeaderBytes ||
      static_cast<std::size_t>(index_off) > len ||
      static_cast<std::size_t>(index_len) > len ||
      static_cast<std::size_t>(index_off) + index_len > len) {
    return Status::BadIndex;
  }

  // Hash covers everything after the hash field (offset 12 → end).
  const std::uint32_t expect =
      fnv1a(data + 12, len - 12u);
  if (expect != out->content_hash) {
    return Status::BadHash;
  }

  const std::uint8_t* ip = data + index_off;
  const std::uint8_t* iend = ip + index_len;
  while (ip < iend) {
    if (static_cast<std::size_t>(iend - ip) < 4u) {
      return Status::BadIndex;
    }
    const std::uint8_t type = ip[0];
    const std::uint8_t id = ip[1];
    const std::uint16_t rec_len = rd_u16(ip + 2);
    ip += 4;
    if (static_cast<std::size_t>(iend - ip) < rec_len) {
      return Status::BadIndex;
    }
    const std::uint8_t* body = ip;
    ip += rec_len;

    if (type == kRecEnd) {
      break;
    }
    if (type == kRecFont) {
      if (out->font_count >= kMaxFonts) {
        return Status::TooMany;
      }
      // body: cell_w, cell_h, advance, first_cp:u16, glyph_count:u16,
      // then glyphs: (cp:u16, off:u32, size:u16)*
      if (rec_len < 7u) {
        return Status::BadIndex;
      }
      FontInfo& f = out->fonts[out->font_count++];
      f.id = id;
      f.cell_w = body[0];
      f.cell_h = body[1];
      f.advance = body[2];
      f.first_cp = rd_u16(body + 3);
      f.glyph_count = rd_u16(body + 5);
      if (f.glyph_count > kMaxGlyphsPerFont) {
        return Status::TooMany;
      }
      const std::size_t need = 7u + static_cast<std::size_t>(f.glyph_count) * 8u;
      if (rec_len < need) {
        return Status::BadIndex;
      }
      for (std::uint16_t g = 0u; g < f.glyph_count; ++g) {
        const std::uint8_t* gp = body + 7u + g * 8u;
        f.glyphs[g].codepoint = rd_u16(gp);
        f.glyphs[g].offset = rd_u32(gp + 2);
        f.glyphs[g].size = rd_u16(gp + 6);
        if (f.glyphs[g].offset >= len ||
            static_cast<std::size_t>(f.glyphs[g].offset) + f.glyphs[g].size > len) {
          return Status::OutOfRange;
        }
      }
    } else if (type == kRecAtlas) {
      if (out->atlas_count >= kMaxAtlases) {
        return Status::TooMany;
      }
      // body: icon_w, icon_h, icon_count:u16, then (id:u16, off:u32, size:u16)*
      if (rec_len < 4u) {
        return Status::BadIndex;
      }
      AtlasInfo& a = out->atlases[out->atlas_count++];
      a.id = id;
      a.icon_w = body[0];
      a.icon_h = body[1];
      a.icon_count = rd_u16(body + 2);
      if (a.icon_count > kMaxIconsPerAtlas) {
        return Status::TooMany;
      }
      const std::size_t need = 4u + static_cast<std::size_t>(a.icon_count) * 8u;
      if (rec_len < need) {
        return Status::BadIndex;
      }
      for (std::uint16_t i = 0u; i < a.icon_count; ++i) {
        const std::uint8_t* gp = body + 4u + i * 8u;
        a.icons[i].id = rd_u16(gp);
        a.icons[i].offset = rd_u32(gp + 2);
        a.icons[i].size = rd_u16(gp + 6);
        if (a.icons[i].offset >= len ||
            static_cast<std::size_t>(a.icons[i].offset) + a.icons[i].size > len) {
          return Status::OutOfRange;
        }
      }
    } else {
      // Unknown — skip by length (forward compatibility).
    }
  }
  return Status::Ok;
}

const FontInfo* find_font(const Index& idx, std::uint8_t font_id) {
  for (std::uint8_t i = 0u; i < idx.font_count; ++i) {
    if (idx.fonts[i].id == font_id) {
      return &idx.fonts[i];
    }
  }
  return nullptr;
}

const GlyphDir* find_glyph(const FontInfo& font, std::uint16_t codepoint) {
  for (std::uint16_t i = 0u; i < font.glyph_count; ++i) {
    if (font.glyphs[i].codepoint == codepoint) {
      return &font.glyphs[i];
    }
  }
  return nullptr;
}

const AtlasInfo* find_atlas(const Index& idx, std::uint8_t atlas_id) {
  for (std::uint8_t i = 0u; i < idx.atlas_count; ++i) {
    if (idx.atlases[i].id == atlas_id) {
      return &idx.atlases[i];
    }
  }
  return nullptr;
}

const IconDir* find_icon(const AtlasInfo& atlas, std::uint16_t icon_id) {
  for (std::uint16_t i = 0u; i < atlas.icon_count; ++i) {
    if (atlas.icons[i].id == icon_id) {
      return &atlas.icons[i];
    }
  }
  return nullptr;
}

}  // namespace pack
}  // namespace slate
