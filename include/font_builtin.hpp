#pragma once

#include <cstdint>

// Built-in font 0 — generated from shared/fonts/font0_3x5.json. Do not edit by hand.

namespace font::builtin3x5 {

constexpr std::uint8_t kId = 0u;
constexpr std::uint8_t kCellWidth = 3u;
constexpr std::uint8_t kCellHeight = 5u;
constexpr std::uint8_t kAdvance = 4u;
constexpr std::uint8_t kFirstCodepoint = 48u;
constexpr std::uint8_t kGlyphCount = 10u;

constexpr std::uint8_t kRows[kGlyphCount][kCellHeight] = {
    {7, 5, 5, 5, 7},
    {2, 2, 2, 2, 2},
    {7, 1, 7, 4, 7},
    {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7},
    {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7},
    {7, 5, 7, 1, 7}
};

}  // namespace font::builtin3x5
