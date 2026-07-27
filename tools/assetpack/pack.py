#!/usr/bin/env python3
"""Build Slate .slap asset packs and emit shared ID constants.

One source of truth for firmware / Kotlin DSL / JS builder font & atlas IDs.

Usage:
  python tools/assetpack/pack.py \\
      --font shared/fonts/font0_3x5.json \\
      --icon-atlas shared/icons/cat16.json \\
      --out shared/generated/core.slap \\
      --emit-ids shared/generated/asset_ids.json

Optional TTF (requires fontTools + Pillow):
  python tools/assetpack/pack.py --ttf path.ttf --font-id 1 --px 12 ...
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def pack_font_record(font: dict) -> tuple[bytes, bytes, list]:
    """Return (payload_bytes, index_body_prefix, dirs)."""
    cell_w = int(font["cell_width"])
    cell_h = int(font["cell_height"])
    advance = int(font["advance"])
    first_cp = int(font["first_codepoint"])
    glyphs = font["glyphs"]
    payload = bytearray()
    dirs = []
    for g in glyphs:
        rows = g["rows"]
        # 1 byte per row, low bits = left pixels (matches builtin header).
        blob = bytes(int(r) & 0xFF for r in rows)
        off = len(payload)  # relative; adjusted later
        dirs.append((int(g["codepoint"]), off, len(blob)))
        payload.extend(blob)

    body = bytearray()
    body.append(cell_w & 0xFF)
    body.append(cell_h & 0xFF)
    body.append(advance & 0xFF)
    body += struct.pack("<HH", first_cp, len(glyphs))
    # offsets filled after we know payload base
    return bytes(payload), bytes(body), dirs


def pack_atlas_record(atlas: dict) -> tuple[bytes, bytes, list]:
    icon_w = int(atlas["icon_width"])
    icon_h = int(atlas["icon_height"])
    icons = atlas["icons"]
    payload = bytearray()
    dirs = []
    for ic in icons:
        # rows: list of bytes, 1bpp packed MSB-left, or raw "bitmap" hex.
        if "rows" in ic:
            blob = bytes(int(r) & 0xFF for r in ic["rows"])
        elif "bitmap_hex" in ic:
            blob = bytes.fromhex(ic["bitmap_hex"])
        else:
            raise SystemExit(f"icon {ic.get('id')} missing rows/bitmap_hex")
        dirs.append((int(ic["id"]), len(payload), len(blob)))
        payload.extend(blob)
    body = bytearray()
    body.append(icon_w & 0xFF)
    body.append(icon_h & 0xFF)
    body += struct.pack("<H", len(icons))
    return bytes(payload), bytes(body), dirs


def build_pack(fonts: list[dict], atlases: list[dict]) -> bytes:
    # Build payload contiguous, then index with absolute offsets.
    payload = bytearray()
    font_meta = []  # (id, body_prefix, dirs with relative offs)
    atlas_meta = []

    for font in fonts:
        pl, body_prefix, dirs = pack_font_record(font)
        base = len(payload)
        payload += pl
        abs_dirs = [(cp, base + off, sz) for cp, off, sz in dirs]
        font_meta.append((int(font["id"]), body_prefix, abs_dirs))

    for atlas in atlases:
        pl, body_prefix, dirs = pack_atlas_record(atlas)
        base = len(payload)
        payload += pl
        abs_dirs = [(iid, base + off, sz) for iid, off, sz in dirs]
        atlas_meta.append((int(atlas["id"]), body_prefix, abs_dirs))

    # Header is 20 bytes; payload starts at 20.
    header_len = 20
    payload_abs = bytearray()
    # Relocate: dirs currently relative to payload start 0; absolute = header_len + off
    index = bytearray()

    def emit_font(fid: int, body_prefix: bytes, dirs: list) -> None:
        body = bytearray(body_prefix)
        for cp, off, sz in dirs:
            body += struct.pack("<HIH", cp, header_len + off, sz)
        index.append(0x01)  # FONT
        index.append(fid & 0xFF)
        index.extend(struct.pack("<H", len(body)))
        index.extend(body)

    def emit_atlas(aid: int, body_prefix: bytes, dirs: list) -> None:
        body = bytearray(body_prefix)
        for iid, off, sz in dirs:
            body += struct.pack("<HIH", iid, header_len + off, sz)
        index.append(0x02)
        index.append(aid & 0xFF)
        index.extend(struct.pack("<H", len(body)))
        index.extend(body)

    for fid, pref, dirs in font_meta:
        emit_font(fid, pref, dirs)
    for aid, pref, dirs in atlas_meta:
        emit_atlas(aid, pref, dirs)
    index.append(0xF0)
    index.append(0)
    index += struct.pack("<H", 0)

    index_offset = header_len + len(payload)
    index_length = len(index)

    # content_hash over bytes from offset 12 (index_offset field) to EOF.
    # Build without hash first.
    rest = struct.pack("<II", index_offset, index_length) + bytes(payload) + bytes(index)
    content_hash = fnv1a(rest)
    header = b"SLAP" + struct.pack("<HHI", 1, 0, content_hash)
    return header + rest


def try_ttf(path: Path, font_id: int, px: int, name: str) -> dict:
    try:
        from fontTools.ttLib import TTFont  # type: ignore
        from fontTools.pens.ttGlyphPen import TTGlyphPen  # noqa: F401
    except ImportError as e:
        raise SystemExit(
            "TTF support requires fontTools. Install or pass --font JSON instead."
        ) from e
    # Minimal: rasterize digits 0-9 via Pillow if available.
    try:
        from PIL import Image, ImageDraw, ImageFont  # type: ignore
    except ImportError as e:
        raise SystemExit("TTF rasterization requires Pillow.") from e

    font = ImageFont.truetype(str(path), px)
    glyphs = []
    cell_w = px
    cell_h = px
    for cp in range(48, 58):
        img = Image.new("1", (cell_w, cell_h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((0, 0), chr(cp), fill=1, font=font)
        rows = []
        for y in range(cell_h):
            bits = 0
            for x in range(min(8, cell_w)):
                if img.getpixel((x, y)):
                    bits |= 1 << (7 - x)
            rows.append(bits)
        glyphs.append({"codepoint": cp, "rows": rows})
    return {
        "id": font_id,
        "name": name,
        "cell_width": min(8, cell_w),
        "cell_height": cell_h,
        "advance": min(8, cell_w) + 1,
        "first_codepoint": 48,
        "glyph_count": len(glyphs),
        "glyphs": glyphs,
    }


def emit_ids(path: Path, fonts: list[dict], atlases: list[dict]) -> None:
    doc = {
        "max_font_id": max((int(f["id"]) for f in fonts), default=0),
        "max_atlas_id": max((int(a["id"]) for a in atlases), default=0),
        "fonts": [{"id": int(f["id"]), "name": f.get("name", f"font{f['id']}")} for f in fonts],
        "atlases": [
            {"id": int(a["id"]), "name": a.get("name", f"atlas{a['id']}")} for a in atlases
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")

    # C++ snippet consumed by firmware / checked into include/
    hpp = path.with_suffix(".hpp")
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "// Generated by tools/assetpack/pack.py — do not edit.",
        "namespace slate { namespace asset_ids {",
        f"constexpr std::uint8_t kMaxFontId = {doc['max_font_id']}u;",
        f"constexpr std::uint8_t kMaxAtlasId = {doc['max_atlas_id']}u;",
    ]
    for f in doc["fonts"]:
        safe = "".join(c if c.isalnum() else "_" for c in f["name"]).upper()
        lines.append(f"constexpr std::uint8_t kFont_{safe} = {f['id']}u;")
    for a in doc["atlases"]:
        safe = "".join(c if c.isalnum() else "_" for c in a["name"]).upper()
        lines.append(f"constexpr std::uint8_t kAtlas_{safe} = {a['id']}u;")
    lines += ["}}  // namespace slate::asset_ids", ""]
    hpp.write_text("\n".join(lines), encoding="utf-8")

    # Kotlin object for companion
    kt_dir = ROOT / "companion" / "sdp-core" / "src" / "main" / "kotlin" / "generated"
    kt_dir.mkdir(parents=True, exist_ok=True)
    kt = kt_dir / "AssetIds.kt"
    klines = [
        "package slate.generated",
        "",
        "/** Generated by tools/assetpack/pack.py — do not edit. */",
        "object AssetIds {",
        f"    const val MAX_FONT_ID = {doc['max_font_id']}",
        f"    const val MAX_ATLAS_ID = {doc['max_atlas_id']}",
    ]
    for f in doc["fonts"]:
        safe = "".join(c if c.isalnum() else "_" for c in f["name"]).upper()
        klines.append(f"    const val FONT_{safe} = {f['id']}")
    for a in doc["atlases"]:
        safe = "".join(c if c.isalnum() else "_" for c in a["name"]).upper()
        klines.append(f"    const val ATLAS_{safe} = {a['id']}")
    klines += ["}", ""]
    kt.write_text("\n".join(klines), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--font", action="append", default=[], help="font JSON (repeatable)")
    ap.add_argument("--icon-atlas", action="append", default=[], help="atlas JSON")
    ap.add_argument("--ttf", type=Path, help="optional TTF path")
    ap.add_argument("--font-id", type=int, default=1)
    ap.add_argument("--px", type=int, default=12)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--emit-ids", type=Path, default=ROOT / "shared/generated/asset_ids.json")
    args = ap.parse_args()

    fonts: list[dict] = []
    for p in args.font:
        fonts.append(json.loads(Path(p).read_text(encoding="utf-8")))
    if args.ttf:
        fonts.append(try_ttf(args.ttf, args.font_id, args.px, args.ttf.stem))
    if not fonts:
        # Default to built-in font0
        fonts.append(
            json.loads((ROOT / "shared/fonts/font0_3x5.json").read_text(encoding="utf-8"))
        )

    atlases: list[dict] = []
    for p in args.icon_atlas:
        atlases.append(json.loads(Path(p).read_text(encoding="utf-8")))
    if not atlases:
        default_atlas = ROOT / "shared/icons/cat16.json"
        if default_atlas.exists():
            atlases.append(json.loads(default_atlas.read_text(encoding="utf-8")))

    blob = build_pack(fonts, atlases)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    emit_ids(args.emit_ids, fonts, atlases)
    print(f"wrote {args.out} ({len(blob)} bytes)")
    print(f"ids -> {args.emit_ids}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
