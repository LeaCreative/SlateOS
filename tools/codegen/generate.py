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
FONT_JSON = SHARED / "fonts" / "font0_3x5.json"
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


def gen_font_hpp(font: dict) -> str:
    rows_arrays = []
    for g in font["glyphs"]:
        row_str = ", ".join(str(r) for r in g["rows"])
        rows_arrays.append(f"    {{{row_str}}}")
    rows_body = ",\n".join(rows_arrays)
    return f"""#pragma once

#include <cstdint>

// Built-in font 0 — generated from shared/fonts/font0_3x5.json. Do not edit by hand.

namespace font::builtin3x5 {{

constexpr std::uint8_t kId = {font["id"]}u;
constexpr std::uint8_t kCellWidth = {font["cell_width"]}u;
constexpr std::uint8_t kCellHeight = {font["cell_height"]}u;
constexpr std::uint8_t kAdvance = {font["advance"]}u;
constexpr std::uint8_t kFirstCodepoint = {font["first_codepoint"]}u;
constexpr std::uint8_t kGlyphCount = {font["glyph_count"]}u;

constexpr std::uint8_t kRows[kGlyphCount][kCellHeight] = {{
{rows_body}
}};

}}  // namespace font::builtin3x5
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

    lines.append("}")
    return "\n".join(lines) + "\n"


def gen_kotlin_font(font: dict) -> str:
    glyph_lines = []
    for g in font["glyphs"]:
        rows = ", ".join(str(r) for r in g["rows"])
        glyph_lines.append(f"        Glyph({g['codepoint']}, byteArrayOf({rows})),")

    glyphs = "\n".join(glyph_lines)
    return (
        "package slate.generated\n\n"
        "/** Built-in font 0 — generated from shared/fonts/font0_3x5.json. */\n"
        "object FontBuiltin3x5 {\n"
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
    font = load_json(FONT_JSON)

    verify_opcodes_match_hpp(wire)

    OUT_FONT_HPP.write_text(gen_font_hpp(font), encoding="utf-8", newline="\n")
    print(f"wrote {OUT_FONT_HPP}")

    OUT_KOTLIN_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_KOTLIN_DIR / "SdpWire.kt").write_text(gen_kotlin_wire(wire), encoding="utf-8", newline="\n")
    (OUT_KOTLIN_DIR / "FontBuiltin3x5.kt").write_text(gen_kotlin_font(font), encoding="utf-8", newline="\n")
    print(f"wrote Kotlin bindings under {OUT_KOTLIN_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
