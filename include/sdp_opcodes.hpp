#pragma once

#include <cstdint>

// SDP wire constants — single origin for firmware, Kotlin DSL, and JS builder.
// Normative definition: slate-implementation-roadmap.md §4.3 / §4.5 / §7.2.
// Do not transcribe these values elsewhere.

namespace sdp {

constexpr std::uint32_t kDisplaySize   = 240u;
constexpr std::uint32_t kMaxListBytes  = 4096u;
constexpr std::uint32_t kMaxOps        = 512u;
constexpr std::uint32_t kPaletteSize   = 16u;
constexpr std::uint32_t kMaxElemDepth  = 8u;
constexpr std::uint32_t kMaxHitElems   = 32u;

// M3 registered ID ranges (until M11 asset packs expand them).
constexpr std::uint8_t  kMaxFontId     = 0u;   // font 0 = built-in 3×5
constexpr std::uint8_t  kMaxAtlasId    = 0u;
constexpr std::uint8_t  kMaxAssetId    = 0u;
constexpr std::uint16_t kMaxIconId     = 0u;   // atlas 0 icon 0 only (stub)
constexpr std::uint16_t kMaxImageId    = 0u;

// ── Opcodes (§4.3) ───────────────────────────────────────────────────────────

namespace op {
constexpr std::uint8_t CLEAR         = 0x01u;
constexpr std::uint8_t SET_PALETTE   = 0x02u;
constexpr std::uint8_t RECT          = 0x03u;
constexpr std::uint8_t RECT_ROUND    = 0x04u;
constexpr std::uint8_t LINE          = 0x05u;
constexpr std::uint8_t CIRCLE        = 0x06u;
constexpr std::uint8_t ARC           = 0x07u;
constexpr std::uint8_t POLYLINE      = 0x08u;
constexpr std::uint8_t CLIP_RECT     = 0x09u;
constexpr std::uint8_t CLIP_CLEAR    = 0x0Au;
constexpr std::uint8_t TEXT          = 0x10u;
constexpr std::uint8_t TEXT_BOX      = 0x11u;
constexpr std::uint8_t ICON          = 0x12u;
constexpr std::uint8_t IMAGE         = 0x13u;
constexpr std::uint8_t PROGRESS_BAR  = 0x20u;
constexpr std::uint8_t PROGRESS_ARC  = 0x21u;
constexpr std::uint8_t BEGIN_ELEM    = 0x30u;
constexpr std::uint8_t END_ELEM      = 0x31u;
constexpr std::uint8_t SCROLL_REGION = 0x40u;
constexpr std::uint8_t PATCH         = 0x50u;
constexpr std::uint8_t PATCH_REF     = 0x51u;
constexpr std::uint8_t HAPTIC        = 0x60u;
constexpr std::uint8_t BACKLIGHT     = 0x61u;
constexpr std::uint8_t EXT_MIN       = 0xE0u;
constexpr std::uint8_t EXT_MAX       = 0xEFu;
constexpr std::uint8_t COMMIT        = 0xF0u;
constexpr std::uint8_t RETAIN        = 0xF1u;
}  // namespace op

// ── COLOR (§4.3) ─────────────────────────────────────────────────────────────

namespace color_tag {
constexpr std::uint8_t LiteralRgb565 = 0x00u;
constexpr std::uint8_t PaletteMin    = 0x01u;
constexpr std::uint8_t PaletteMax    = 0x10u;
// 0x11–0xFF reserved — reject
}  // namespace color_tag

// ── STYLE (§4.3) ─────────────────────────────────────────────────────────────

namespace style {
constexpr std::uint8_t ModeFill        = 0u;
constexpr std::uint8_t ModeStroke      = 1u;
constexpr std::uint8_t ModeFillStroke  = 2u;
constexpr std::uint8_t ModeReserved    = 3u;  // reject
constexpr std::uint8_t ModeMask        = 0x03u;
constexpr std::uint8_t WidthShift      = 2u;
constexpr std::uint8_t WidthMask       = 0x0Fu;
constexpr std::uint8_t ReservedMask    = 0xC0u;  // bits 6-7 must be zero
}  // namespace style

// ── BEGIN_ELEM flags (§4.5) ──────────────────────────────────────────────────

namespace elem_flags {
constexpr std::uint8_t EMIT_TOUCH = 1u << 0;
constexpr std::uint8_t NO_HIT     = 1u << 1;
constexpr std::uint8_t HAPTIC     = 1u << 2;
constexpr std::uint8_t FOCUSABLE  = 1u << 3;
constexpr std::uint8_t DISABLED   = 1u << 4;
constexpr std::uint8_t Allowed    = EMIT_TOUCH | NO_HIT | HAPTIC | FOCUSABLE | DISABLED;
}  // namespace elem_flags

// ── TEXT_BOX flags (§4.5) ────────────────────────────────────────────────────

namespace text_box_flags {
constexpr std::uint8_t WRAP         = 1u << 0;
constexpr std::uint8_t ELLIPSIS_END = 1u << 1;
constexpr std::uint8_t VCENTER      = 1u << 2;
constexpr std::uint8_t Allowed      = WRAP | ELLIPSIS_END | VCENTER;
}  // namespace text_box_flags

// ── COMMIT flags (§4.5) ──────────────────────────────────────────────────────

namespace commit_flags {
constexpr std::uint8_t FADE     = 1u << 0;
constexpr std::uint8_t NO_CLEAR = 1u << 1;
constexpr std::uint8_t Allowed  = FADE | NO_CLEAR;
}  // namespace commit_flags

// ── Single-value enumerations (§4.5) ─────────────────────────────────────────

namespace align {
constexpr std::uint8_t LEFT   = 0u;
constexpr std::uint8_t CENTER = 1u;
constexpr std::uint8_t RIGHT  = 2u;
constexpr std::uint8_t Max    = 2u;
}  // namespace align

namespace patch_format {
constexpr std::uint8_t RGB565 = 0u;
constexpr std::uint8_t RGB332 = 1u;
constexpr std::uint8_t PAL4   = 2u;
constexpr std::uint8_t MONO1  = 3u;
constexpr std::uint8_t Max    = 3u;
}  // namespace patch_format

namespace patch_encoding {
constexpr std::uint8_t RAW = 0u;
constexpr std::uint8_t RLE = 1u;
constexpr std::uint8_t Max = 1u;
}  // namespace patch_encoding

namespace haptic_pattern {
constexpr std::uint8_t TICK   = 0u;
constexpr std::uint8_t SHORT  = 1u;
constexpr std::uint8_t DOUBLE = 2u;
constexpr std::uint8_t LONG   = 3u;
constexpr std::uint8_t ERROR  = 4u;
constexpr std::uint8_t Max    = 4u;
}  // namespace haptic_pattern

}  // namespace sdp
