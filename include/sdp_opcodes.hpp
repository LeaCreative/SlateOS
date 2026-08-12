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
// Runtime packs may raise these; see shared/generated/asset_ids.hpp from pack.py.
// 0 = built-in 3x5 (dense, diagnostic overlay), 1 = built-in 5x7 (legible).
// Both are compiled in; see include/font_builtin.hpp. The parser rejects any
// text op above this, so it must track shared/sdp_wire.json — tools/codegen
// cross-checks the two on every run.
constexpr std::uint8_t  kMaxFontId     = 1u;
// TEXT_SCALED multiplies each glyph pixel into a scale×scale block. 1 = native.
constexpr std::uint8_t  kMinTextScale  = 1u;
constexpr std::uint8_t  kMaxTextScale  = 16u;
constexpr std::uint8_t  kMaxAtlasId    = 0u;
constexpr std::uint8_t  kMaxAssetId    = 0u;
constexpr std::uint16_t kMaxIconId     = 2u;   // category atlas icons 0..2
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
// Length-prefixed extension: TEXT plus an integer pixel scale. Firmware that
// predates it skips the op by its length instead of faulting (§7.2).
constexpr std::uint8_t TEXT_SCALED   = 0xE0u;
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
constexpr std::uint8_t TICK         = 0u;
constexpr std::uint8_t SHORT        = 1u;
constexpr std::uint8_t DOUBLE       = 2u;
constexpr std::uint8_t LONG         = 3u;
constexpr std::uint8_t ERROR        = 4u;
constexpr std::uint8_t TRIPLE_LONG  = 5u;  // incoming call: 3× long pulses
constexpr std::uint8_t Max          = 5u;
}  // namespace haptic_pattern

// ── Input channel ops (§4.4) ─────────────────────────────────────────────────

namespace input_op {
constexpr std::uint8_t TAP          = 0x01u;
constexpr std::uint8_t LONG_PRESS   = 0x02u;
constexpr std::uint8_t SWIPE        = 0x03u;
constexpr std::uint8_t BUTTON       = 0x04u;
constexpr std::uint8_t SCROLL_POS   = 0x05u;
constexpr std::uint8_t BACK         = 0x06u;
constexpr std::uint8_t SESSION_END  = 0x07u;
constexpr std::uint8_t MULTI_TAP    = 0x08u;
constexpr std::uint8_t EDGE_SWIPE   = 0x09u;
constexpr std::uint8_t TOUCH_DOWN   = 0x0Au;
constexpr std::uint8_t TOUCH_UP     = 0x0Bu;
// 0x0C–0x1F reserved
// Length-prefixed extensions (0xE0–0xEF): old phones ignore unknown INPUT ops.
constexpr std::uint8_t NOTIF_REQ    = 0xE2u;  // watch → phone: [op][key_len][key…]
}  // namespace input_op

namespace swipe_dir {
constexpr std::uint8_t UP    = 0u;
constexpr std::uint8_t DOWN  = 1u;
constexpr std::uint8_t LEFT  = 2u;
constexpr std::uint8_t RIGHT = 3u;
constexpr std::uint8_t Max   = 3u;
}  // namespace swipe_dir

namespace edge {
constexpr std::uint8_t TOP    = 0u;
constexpr std::uint8_t BOTTOM = 1u;
constexpr std::uint8_t LEFT   = 2u;
constexpr std::uint8_t RIGHT  = 3u;
constexpr std::uint8_t Max    = 3u;
}  // namespace edge

namespace button_action {
constexpr std::uint8_t PRESS        = 0u;
constexpr std::uint8_t LONG_PRESS   = 1u;
constexpr std::uint8_t DOUBLE_PRESS = 2u;
constexpr std::uint8_t Max          = 2u;
}  // namespace button_action

namespace session_end_reason {
constexpr std::uint8_t USER_BACK      = 0u;
constexpr std::uint8_t TIMEOUT        = 1u;
constexpr std::uint8_t LINK_LOST      = 2u;
constexpr std::uint8_t PHONE_REQUEST  = 3u;
constexpr std::uint8_t ERROR         = 4u;
constexpr std::uint8_t Max            = 4u;
}  // namespace session_end_reason

// ── CONTROL channel ops (§4.6 — firmware-defined wire) ───────────────────────

namespace control_op {
constexpr std::uint8_t HELLO_OFFER    = 0x01u;  // watch → phone
constexpr std::uint8_t HELLO_ACCEPT   = 0x02u;  // phone → watch
constexpr std::uint8_t HELLO_REJECT   = 0x03u;  // phone → watch
constexpr std::uint8_t HEARTBEAT      = 0x04u;  // phone → watch (keep-alive)
constexpr std::uint8_t SET_PROFILE    = 0x05u;  // phone → watch (id only)
constexpr std::uint8_t PROFILE_ACK    = 0x06u;  // watch → phone
constexpr std::uint8_t SCREEN_PUSH    = 0x07u;  // phone → watch (next DISPLAY pushes)
constexpr std::uint8_t SCREEN_POP     = 0x08u;  // phone → watch
constexpr std::uint8_t SCREEN_REPLACE = 0x09u;  // phone → watch (next DISPLAY replaces)
constexpr std::uint8_t CREDIT         = 0x0Au;  // watch → phone (free DL bytes)
constexpr std::uint8_t GOODBYE        = 0x0Bu;  // either
// Wall-clock sync (not GATT CTS): op + unix epoch u32 LE. Companion TimeSync.kt.
constexpr std::uint8_t TIME_SYNC      = 0x20u;  // phone → watch
/**
 * Watch settings, synced both directions. Same message either way:
 *
 *   [op][wire_version:u8][revision:u32 LE][tilt_enabled][wake_seconds]
 *   [face_show_steps][face_show_diag][hr_enabled]
 *   [ui_chrome:u16 LE][face_bright:u16 LE][face_dim:u16 LE]
 *   [raise_sensitivity][shake_enabled][shake_sensitivity]   = 20 bytes (v4)
 *
 * `revision` is a Lamport counter, not a plain sequence: a side that applies a
 * remote update adopts its revision, and a local edit sets
 * revision = max(seen) + 1. That is what makes "whichever side changed last
 * wins" actually true when both have edited since they last spoke — two
 * independent counters would both sit at the same number with different
 * content and there would be nothing to choose between them.
 *
 * Ties (equal revision, different content) resolve to the WATCH, because it is
 * the device that cannot show a merge conflict.
 */
constexpr std::uint8_t SETTINGS_SYNC  = 0x21u;  // either direction
// Trial-image confirm status (0xE0–0xEF CONTROL extension; old FW ignores).
constexpr std::uint8_t CONFIRM_STATUS_REQUEST = 0xE0u;  // phone → watch
constexpr std::uint8_t CONFIRM_STATUS         = 0xE1u;  // watch → phone
// CONFIRM_STATUS payload: [op][needs_confirm:u8][dwell_ms_remaining:u32 LE]
/**
 * Watch → phone vitals for Health Connect (0xE2 CONTROL extension).
 * Payload: [op][steps:u32 LE][bpm:u8][pad:u8]  (7 bytes). Cadence is host-
 * friendly (change≤1/min, else ≤5 min keepalive) — not every PPG sample.
 */
constexpr std::uint8_t VITALS                 = 0xE2u;  // watch → phone
}  // namespace control_op

constexpr std::uint16_t kProtocolVersion = 1u;
constexpr std::uint8_t  kFwMajor = 0u;
constexpr std::uint8_t  kFwMinor = 11u;  // M11 asset packs
constexpr std::uint8_t  kFwPatch = 0u;
constexpr std::uint8_t  kOpcodeBitmapBytes = 32u;
constexpr std::uint8_t  kPhoneIdBytes = 8u;
constexpr std::uint8_t  kMaxProfileNameLen = 16u;
constexpr std::uint32_t kHeartbeatPeriodMs = 2000u;
constexpr std::uint8_t  kHeartbeatStaleMisses = 2u;
constexpr std::uint8_t  kHeartbeatDropMisses = 5u;

}  // namespace sdp
