#!/usr/bin/env python3
"""Generate Kotlin wire/font bindings and firmware font header from shared/ assets."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHARED = ROOT / "shared"
WIRE_JSON = SHARED / "sdp_wire.json"
FONT_JSONS = [
    SHARED / "fonts" / "font0_3x5.json",
    SHARED / "fonts" / "font1_5x7.json",
]
HPP_OPCODES = ROOT / "include" / "sdp_opcodes.hpp"
OUT_FONT_HPP = ROOT / "include" / "font_builtin.hpp"
OUT_KOTLIN_DIR = ROOT / "companion" / "sdp-core" / "src" / "main" / "kotlin" / "generated"


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def verify_opcodes_match_hpp(wire: dict) -> None:
    if not HPP_OPCODES.exists():
        print(f"warning: {HPP_OPCODES} missing — skip opcode cross-check")
        return
    text = HPP_OPCODES.read_text(encoding="utf-8")
    op = wire["op"]
    for name, value in op.items():
        pattern = rf"constexpr std::uint8_t {re.escape(name)}\s*=\s*0x([0-9A-Fa-f]+)u"
        m = re.search(pattern, text)
        if not m:
            raise SystemExit(f"sdp_opcodes.hpp missing op::{name}")
        hpp_val = int(m.group(1), 16)
        if hpp_val != value:
            raise SystemExit(
                f"opcode mismatch {name}: json={value} hpp=0x{hpp_val:x}"
            )
    print("opcode cross-check: sdp_wire.json matches include/sdp_opcodes.hpp")


def gen_font_hpp(fonts: list) -> str:
    """Emit every built-in font plus a Desc table the renderer selects on."""
    blocks = []
    descs = []
    for f in fonts:
        ns = f["name"].replace("builtin_", "builtin")
        rows_body = ",\n".join(
            "    {" + ", ".join(str(r) for r in g["rows"]) + "}" for g in f["glyphs"]
        )
        blocks.append(f"""namespace {ns} {{

constexpr std::uint8_t kId = {f["id"]}u;
constexpr std::uint8_t kCellWidth = {f["cell_width"]}u;
constexpr std::uint8_t kCellHeight = {f["cell_height"]}u;
constexpr std::uint8_t kAdvance = {f["advance"]}u;
constexpr std::uint8_t kFirstCodepoint = {f["first_codepoint"]}u;
constexpr std::uint8_t kGlyphCount = {f["glyph_count"]}u;

constexpr std::uint8_t kRows[kGlyphCount][kCellHeight] = {{
{rows_body}
}};

}}  // namespace {ns}""")
        descs.append(
            f"    Desc{{{ns}::kId, {ns}::kCellWidth, {ns}::kCellHeight, "
            f"{ns}::kAdvance, {ns}::kFirstCodepoint, {ns}::kGlyphCount, "
            f"&{ns}::kRows[0][0]}}"
        )
    body = "\n\n".join(blocks)
    desc_body = ",\n".join(descs)
    picto = fonts[0].get("pictograms", {})
    picto_body = "\n".join(
        f"constexpr char k{name.title()} = '{ch}';" for name, ch in picto.items()
    )
    return f"""#pragma once

#include <cstdint>

// Generated from shared/fonts/*.json by tools/codegen/generate.py.
// Do not edit by hand — edit tools/codegen/font0_art.py / font1_art.py.

namespace font {{

/**
 * One built-in font, resolved from the font id carried by every SDP text op.
 *
 * `rows` is the glyph table flattened: glyph g row r is
 * rows[g * cell_height + r], MSB-left within cell_width bits.
 */
struct Desc {{
  std::uint8_t id;
  std::uint8_t cell_width;
  std::uint8_t cell_height;
  std::uint8_t advance;
  std::uint8_t first_codepoint;
  std::uint8_t glyph_count;
  const std::uint8_t* rows;
}};

}}  // namespace font

namespace font {{

{body}

constexpr Desc kFonts[] = {{
{desc_body}
}};
constexpr std::uint8_t kFontCount =
    static_cast<std::uint8_t>(sizeof(kFonts) / sizeof(kFonts[0]));

/** Unknown ids fall back to font 0 rather than faulting — BLE-facing input. */
constexpr const Desc& describe(std::uint8_t id) {{
  return kFonts[id < kFontCount ? id : 0u];
}}

// Pictogram slots. Never hard-code the punctuation in UI code.
{picto_body}

}}  // namespace font
"""


def gen_kotlin_wire(wire: dict) -> str:
    lines = [
        "package slate.generated",
        "",
        "/** SDP wire constants — generated from shared/sdp_wire.json. */",
        "object SdpWire {",
        f"    const val DISPLAY_SIZE = {wire['display_size']}",
        f"    const val MAX_LIST_BYTES = {wire['max_list_bytes']}",
        f"    const val MAX_OPS = {wire['max_ops']}",
        f"    const val PALETTE_SIZE = {wire['palette_size']}",
        f"    const val MAX_ELEM_DEPTH = {wire['max_elem_depth']}",
        f"    const val MAX_HIT_ELEMS = {wire['max_hit_elems']}",
        f"    const val MAX_FONT_ID = {wire['max_font_id']}",
        f"    const val MAX_ATLAS_ID = {wire['max_atlas_id']}",
        f"    const val MAX_ASSET_ID = {wire['max_asset_id']}",
        f"    const val MAX_ICON_ID = {wire['max_icon_id']}",
        f"    const val MAX_IMAGE_ID = {wire['max_image_id']}",
        f"    const val NO_HIT = 0xFFFF",
        "",
    ]

    def block(name: str, d: dict, indent: str = "    ") -> None:
        lines.append(f"{indent}object {name} {{")
        for k, v in d.items():
            if k == "ALLOWED" or k == "MAX":
                lines.append(f"{indent}    const val {k} = {v}")
            else:
                lines.append(f"{indent}    const val {k} = 0x{v:02X}")
        lines.append(f"{indent}}}")
        lines.append("")

    block("Op", wire["op"])
    block("ColorTag", wire["color_tag"])
    block("Style", wire["style"])
    block("ElemFlags", wire["elem_flags"])
    block("TextBoxFlags", wire["text_box_flags"])
    block("CommitFlags", wire["commit_flags"])
    block("Align", wire["align"])
    block("PatchFormat", wire["patch_format"])
    block("PatchEncoding", wire["patch_encoding"])
    block("HapticPattern", wire["haptic_pattern"])
    block("SwipeDir", wire["swipe_dir"])
    block("InputOp", wire["input_op"])
    block("ButtonAction", wire["button_action"])
    block("SessionEndReason", wire["session_end_reason"])
    block("Edge", wire["edge"])
    if "control_op" in wire:
        block("ControlOp", wire["control_op"])
    if "protocol_version" in wire:
        lines.append(f"    const val PROTOCOL_VERSION = {wire['protocol_version']}")
        lines.append("")

    lines.append("}")
    return "\n".join(lines) + "\n"


def gen_kotlin_font(font: dict, object_name: str) -> str:
    glyph_lines = []
    for g in font["glyphs"]:
        rows = ", ".join(str(r) for r in g["rows"])
        glyph_lines.append(f"        Glyph({g['codepoint']}, byteArrayOf({rows})),")

    glyphs = "\n".join(glyph_lines)
    return (
        "package slate.generated\n\n"
        f"/** Built-in font {font['id']} ({font['name']}) — generated from "
        f"shared/fonts/. */\n"
        f"object {object_name} {{\n"
        f"    const val ID = {font['id']}\n"
        f"    const val CELL_WIDTH = {font['cell_width']}\n"
        f"    const val CELL_HEIGHT = {font['cell_height']}\n"
        f"    const val ADVANCE = {font['advance']}\n"
        f"    const val FIRST_CODEPOINT = {font['first_codepoint']}\n"
        f"    const val GLYPH_COUNT = {font['glyph_count']}\n\n"
        "    data class Glyph(val codepoint: Int, val rows: ByteArray)\n\n"
        "    val GLYPHS: List<Glyph> = listOf(\n"
        f"{glyphs}\n"
        "    )\n\n"
        "    fun rowsFor(codepoint: Int): ByteArray? =\n"
        "        GLYPHS.firstOrNull { it.codepoint == codepoint }?.rows\n"
        "}\n"
    )


def main() -> int:
    wire = load_json(WIRE_JSON)
    fonts = [load_json(p) for p in FONT_JSONS]

    verify_opcodes_match_hpp(wire)

    # Three copies of this number have to agree or text silently stops
    # rendering: the fonts themselves, the wire spec, and the firmware parser
    # (which rejects any text op above kMaxFontId). Checked here so a new font
    # cannot be half-added.
    max_font_id = max(f["id"] for f in fonts)
    if wire["max_font_id"] != max_font_id:
        raise SystemExit(
            f"shared/sdp_wire.json max_font_id={wire['max_font_id']} but "
            f"shared/fonts/ defines ids up to {max_font_id}"
        )
    hpp = HPP_OPCODES.read_text(encoding="utf-8")
    m = re.search(r"kMaxFontId\s*=\s*(\d+)u", hpp)
    if not m:
        raise SystemExit("sdp_opcodes.hpp missing kMaxFontId")
    if int(m.group(1)) != max_font_id:
        raise SystemExit(
            f"sdp_opcodes.hpp kMaxFontId={m.group(1)} but shared/fonts/ "
            f"defines ids up to {max_font_id} — the parser would reject font "
            f"{max_font_id} text ops"
        )
    print(f"font cross-check: max_font_id={max_font_id} agrees across "
          f"fonts/, sdp_wire.json and sdp_opcodes.hpp")

    OUT_FONT_HPP.write_text(gen_font_hpp(fonts), encoding="utf-8", newline="\n")
    print(f"wrote {OUT_FONT_HPP}")

    OUT_KOTLIN_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_KOTLIN_DIR / "SdpWire.kt").write_text(gen_kotlin_wire(wire), encoding="utf-8", newline="\n")
    for f in fonts:
        obj = "FontBuiltin" + f["name"].replace("builtin_", "")
        (OUT_KOTLIN_DIR / f"{obj}.kt").write_text(
            gen_kotlin_font(f, obj), encoding="utf-8", newline="\n")
    print(f"wrote Kotlin bindings under {OUT_KOTLIN_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
