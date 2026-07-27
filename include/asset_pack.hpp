#pragma once

#include <cstddef>
#include <cstdint>

// Slate asset pack (.slap) — host-safe bounded parser.
//
// Layout:
//   magic[4] = 'S','L','A','P'
//   version:u16 LE
//   flags:u16 LE
//   content_hash:u32 LE   // FNV-1a of bytes after this header field
//   index_offset:u32 LE   // absolute offset of index from file start
//   index_length:u32 LE
//   ... payload (glyph/icon bitmaps) ...
//   index: TLV records
//
// Index record:
//   type:u8, id:u8, length:u16 LE, body[length]
// Unknown types MUST be skipped via length (forward-compat).

namespace slate {
namespace pack {

constexpr std::uint8_t kMagic[4] = {'S', 'L', 'A', 'P'};
constexpr std::uint16_t kFormatVersion = 1u;
constexpr std::size_t kHeaderBytes = 20u;

constexpr std::uint8_t kRecFont = 0x01u;
constexpr std::uint8_t kRecAtlas = 0x02u;
constexpr std::uint8_t kRecEnd = 0xF0u;

constexpr std::uint8_t kMaxFonts = 8u;
constexpr std::uint8_t kMaxAtlases = 8u;
constexpr std::uint16_t kMaxGlyphsPerFont = 256u;
constexpr std::uint16_t kMaxIconsPerAtlas = 64u;

enum class Status : std::uint8_t {
  Ok = 0,
  Truncated,
  BadMagic,
  BadVersion,
  BadHash,
  BadIndex,
  TooMany,
  OutOfRange,
};

struct GlyphDir {
  std::uint16_t codepoint = 0u;
  std::uint32_t offset = 0u;  // absolute file offset
  std::uint16_t size = 0u;
};

struct FontInfo {
  std::uint8_t id = 0u;
  std::uint8_t cell_w = 0u;
  std::uint8_t cell_h = 0u;
  std::uint8_t advance = 0u;
  std::uint16_t first_cp = 0u;
  std::uint16_t glyph_count = 0u;
  GlyphDir glyphs[kMaxGlyphsPerFont] = {};
};

struct IconDir {
  std::uint16_t id = 0u;
  std::uint32_t offset = 0u;
  std::uint16_t size = 0u;
};

struct AtlasInfo {
  std::uint8_t id = 0u;
  std::uint8_t icon_w = 0u;
  std::uint8_t icon_h = 0u;
  std::uint16_t icon_count = 0u;
  IconDir icons[kMaxIconsPerAtlas] = {};
};

struct Index {
  std::uint16_t version = 0u;
  std::uint32_t content_hash = 0u;
  std::uint8_t font_count = 0u;
  std::uint8_t atlas_count = 0u;
  FontInfo fonts[kMaxFonts] = {};
  AtlasInfo atlases[kMaxAtlases] = {};
};

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t len);

// Parse header + index from a complete pack buffer (or mapped view).
// Validates bounds exactly like the display-list parser — reject, never fault.
Status parse(const std::uint8_t* data, std::size_t len, Index* out);

// Lookup helpers.
const FontInfo* find_font(const Index& idx, std::uint8_t font_id);
const GlyphDir* find_glyph(const FontInfo& font, std::uint16_t codepoint);
const AtlasInfo* find_atlas(const Index& idx, std::uint8_t atlas_id);
const IconDir* find_icon(const AtlasInfo& atlas, std::uint16_t icon_id);

}  // namespace pack
}  // namespace slate
