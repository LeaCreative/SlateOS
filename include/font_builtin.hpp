#pragma once

#include <cstdint>

// Generated from shared/fonts/*.json by tools/codegen/generate.py.
// Do not edit by hand — edit tools/codegen/font0_art.py / font1_art.py.

namespace font {

/**
 * One built-in font, resolved from the font id carried by every SDP text op.
 *
 * `rows` is the glyph table flattened: glyph g row r is
 * rows[g * cell_height + r], MSB-left within cell_width bits.
 */
struct Desc {
  std::uint8_t id;
  std::uint8_t cell_width;
  std::uint8_t cell_height;
  std::uint8_t advance;
  std::uint8_t first_codepoint;
  std::uint8_t glyph_count;
  const std::uint8_t* rows;
};

}  // namespace font

namespace font {

namespace builtin3x5 {

constexpr std::uint8_t kId = 0u;
constexpr std::uint8_t kCellWidth = 3u;
constexpr std::uint8_t kCellHeight = 5u;
constexpr std::uint8_t kAdvance = 4u;
constexpr std::uint8_t kFirstCodepoint = 32u;
constexpr std::uint8_t kGlyphCount = 95u;

constexpr std::uint8_t kRows[kGlyphCount][kCellHeight] = {
    {0, 0, 0, 0, 0},
    {2, 2, 2, 0, 2},
    {5, 5, 0, 0, 0},
    {5, 7, 5, 7, 5},
    {2, 3, 2, 6, 2},
    {5, 1, 2, 4, 5},
    {2, 5, 2, 5, 3},
    {2, 2, 0, 0, 0},
    {1, 2, 2, 2, 1},
    {4, 2, 2, 2, 4},
    {0, 5, 2, 5, 0},
    {0, 2, 7, 2, 0},
    {0, 0, 0, 2, 4},
    {0, 0, 7, 0, 0},
    {0, 0, 0, 0, 2},
    {1, 1, 2, 4, 4},
    {7, 5, 5, 5, 7},
    {2, 2, 2, 2, 2},
    {7, 1, 7, 4, 7},
    {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7},
    {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7},
    {7, 5, 7, 1, 7},
    {0, 2, 0, 2, 0},
    {0, 2, 0, 2, 4},
    {1, 2, 4, 2, 1},
    {0, 7, 0, 7, 0},
    {4, 2, 1, 2, 4},
    {6, 1, 2, 0, 2},
    {2, 5, 7, 4, 3},
    {2, 5, 7, 5, 5},
    {6, 5, 6, 5, 6},
    {3, 4, 4, 4, 3},
    {6, 5, 5, 5, 6},
    {7, 4, 6, 4, 7},
    {7, 4, 6, 4, 4},
    {3, 4, 5, 5, 3},
    {5, 5, 7, 5, 5},
    {7, 2, 2, 2, 7},
    {1, 1, 1, 5, 2},
    {5, 5, 6, 5, 5},
    {4, 4, 4, 4, 7},
    {5, 7, 7, 5, 5},
    {5, 7, 5, 5, 5},
    {2, 5, 5, 5, 2},
    {6, 5, 6, 4, 4},
    {2, 5, 5, 6, 3},
    {6, 5, 6, 5, 5},
    {3, 4, 2, 1, 6},
    {7, 2, 2, 2, 2},
    {5, 5, 5, 5, 7},
    {5, 5, 5, 5, 2},
    {5, 5, 7, 7, 5},
    {5, 5, 2, 5, 5},
    {5, 5, 2, 2, 2},
    {7, 1, 2, 4, 7},
    {3, 2, 2, 2, 3},
    {4, 4, 2, 1, 1},
    {6, 2, 2, 2, 6},
    {2, 5, 0, 0, 0},
    {0, 0, 0, 0, 7},
    {4, 2, 0, 0, 0},
    {0, 3, 5, 5, 3},
    {4, 4, 6, 5, 6},
    {0, 3, 4, 4, 3},
    {1, 1, 3, 5, 3},
    {0, 2, 5, 6, 3},
    {3, 4, 6, 4, 4},
    {0, 3, 5, 3, 6},
    {4, 4, 6, 5, 5},
    {2, 0, 2, 2, 2},
    {1, 0, 1, 5, 2},
    {4, 5, 6, 6, 5},
    {6, 2, 2, 2, 3},
    {0, 7, 7, 5, 5},
    {0, 6, 5, 5, 5},
    {0, 2, 5, 5, 2},
    {0, 6, 5, 6, 4},
    {0, 3, 5, 3, 1},
    {0, 5, 6, 4, 4},
    {0, 3, 6, 1, 6},
    {2, 7, 2, 2, 3},
    {0, 5, 5, 5, 3},
    {0, 5, 5, 5, 2},
    {0, 5, 5, 7, 7},
    {0, 5, 2, 2, 5},
    {0, 5, 5, 3, 6},
    {0, 7, 1, 2, 7},
    {1, 1, 5, 2, 0},
    {0, 5, 2, 5, 0},
    {0, 5, 0, 5, 2},
    {5, 7, 7, 2, 0}
};

}  // namespace builtin3x5

namespace builtin5x7 {

constexpr std::uint8_t kId = 1u;
constexpr std::uint8_t kCellWidth = 5u;
constexpr std::uint8_t kCellHeight = 7u;
constexpr std::uint8_t kAdvance = 6u;
constexpr std::uint8_t kFirstCodepoint = 32u;
constexpr std::uint8_t kGlyphCount = 95u;

constexpr std::uint8_t kRows[kGlyphCount][kCellHeight] = {
    {0, 0, 0, 0, 0, 0, 0},
    {4, 4, 4, 4, 4, 0, 4},
    {10, 10, 0, 0, 0, 0, 0},
    {10, 10, 31, 10, 31, 10, 10},
    {4, 15, 20, 14, 5, 30, 4},
    {24, 25, 2, 4, 8, 19, 3},
    {12, 18, 20, 8, 21, 18, 13},
    {4, 4, 0, 0, 0, 0, 0},
    {2, 4, 8, 8, 8, 4, 2},
    {8, 4, 2, 2, 2, 4, 8},
    {0, 21, 14, 31, 14, 21, 0},
    {0, 4, 4, 31, 4, 4, 0},
    {0, 0, 0, 0, 12, 4, 8},
    {0, 0, 0, 31, 0, 0, 0},
    {0, 0, 0, 0, 0, 12, 12},
    {1, 2, 4, 4, 4, 8, 16},
    {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},
    {31, 2, 4, 2, 1, 17, 14},
    {2, 6, 10, 18, 31, 2, 2},
    {31, 16, 30, 1, 1, 17, 14},
    {6, 8, 16, 30, 17, 17, 14},
    {31, 17, 1, 2, 4, 4, 4},
    {14, 17, 17, 14, 17, 17, 14},
    {14, 17, 17, 15, 1, 2, 12},
    {0, 12, 12, 0, 12, 12, 0},
    {0, 12, 12, 0, 12, 4, 8},
    {2, 4, 8, 16, 8, 4, 2},
    {0, 0, 31, 0, 31, 0, 0},
    {8, 4, 2, 1, 2, 4, 8},
    {14, 17, 1, 2, 4, 0, 4},
    {14, 17, 1, 23, 21, 23, 14},
    {4, 10, 17, 17, 31, 17, 17},
    {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14},
    {28, 18, 17, 17, 17, 18, 28},
    {31, 16, 16, 30, 16, 16, 31},
    {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15},
    {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},
    {1, 1, 1, 1, 17, 17, 14},
    {17, 18, 20, 24, 20, 18, 17},
    {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17},
    {17, 17, 25, 21, 19, 17, 17},
    {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13},
    {30, 17, 17, 30, 20, 18, 17},
    {14, 17, 16, 14, 1, 17, 14},
    {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 27, 17},
    {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},
    {31, 1, 2, 4, 8, 16, 31},
    {14, 8, 8, 8, 8, 8, 14},
    {16, 8, 4, 4, 4, 2, 1},
    {14, 2, 2, 2, 2, 2, 14},
    {4, 10, 17, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 31},
    {8, 4, 0, 0, 0, 0, 0},
    {0, 0, 14, 1, 15, 17, 15},
    {16, 16, 30, 17, 17, 17, 30},
    {0, 0, 14, 17, 16, 17, 14},
    {1, 1, 15, 17, 17, 17, 15},
    {0, 0, 14, 17, 31, 16, 14},
    {6, 9, 8, 28, 8, 8, 8},
    {0, 0, 15, 17, 15, 1, 14},
    {16, 16, 30, 17, 17, 17, 17},
    {4, 0, 12, 4, 4, 4, 14},
    {2, 0, 6, 2, 2, 18, 12},
    {16, 16, 18, 20, 24, 20, 18},
    {12, 4, 4, 4, 4, 4, 14},
    {0, 0, 26, 21, 21, 17, 17},
    {0, 0, 30, 17, 17, 17, 17},
    {0, 0, 14, 17, 17, 17, 14},
    {0, 0, 30, 17, 30, 16, 16},
    {0, 0, 15, 17, 15, 1, 1},
    {0, 0, 22, 25, 16, 16, 16},
    {0, 0, 15, 16, 14, 1, 30},
    {8, 8, 28, 8, 8, 9, 6},
    {0, 0, 17, 17, 17, 19, 13},
    {0, 0, 17, 17, 17, 10, 4},
    {0, 0, 17, 17, 21, 21, 10},
    {0, 0, 17, 10, 4, 10, 17},
    {0, 0, 17, 17, 15, 1, 14},
    {0, 0, 31, 2, 4, 8, 31},
    {0, 1, 2, 18, 10, 4, 0},
    {0, 17, 10, 4, 10, 17, 0},
    {14, 17, 21, 17, 27, 17, 14},
    {0, 10, 31, 31, 14, 4, 0}
};

}  // namespace builtin5x7

constexpr Desc kFonts[] = {
    Desc{builtin3x5::kId, builtin3x5::kCellWidth, builtin3x5::kCellHeight, builtin3x5::kAdvance, builtin3x5::kFirstCodepoint, builtin3x5::kGlyphCount, &builtin3x5::kRows[0][0]},
    Desc{builtin5x7::kId, builtin5x7::kCellWidth, builtin5x7::kCellHeight, builtin5x7::kAdvance, builtin5x7::kFirstCodepoint, builtin5x7::kGlyphCount, &builtin5x7::kRows[0][0]}
};
constexpr std::uint8_t kFontCount =
    static_cast<std::uint8_t>(sizeof(kFonts) / sizeof(kFonts[0]));

/** Unknown ids fall back to font 0 rather than faulting — BLE-facing input. */
constexpr const Desc& describe(std::uint8_t id) {
  return kFonts[id < kFontCount ? id : 0u];
}

// Pictogram slots. Never hard-code the punctuation in UI code.
constexpr char kTick = '{';
constexpr char kCross = '|';
constexpr char kSmiley = '}';
constexpr char kHeart = '~';

}  // namespace font
