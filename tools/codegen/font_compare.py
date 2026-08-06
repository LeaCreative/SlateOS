#!/usr/bin/env python3
"""
Side-by-side proof sheet: built-in 3x5 (font 0) vs candidate 5x7 (font 1).

Both are drawn at the footprint they would actually occupy, which is the whole
point of the comparison: 3x5 at scale 3 and 5x7 at scale 2 take the same
12 px per character. Same width, same line height, very different glyph.
"""

import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT_SVG = ROOT / "build" / "font-compare.svg"

F0 = json.loads((ROOT / "shared" / "fonts" / "font0_3x5.json").read_text(encoding="utf-8"))
F1 = json.loads((ROOT / "shared" / "fonts" / "font1_5x7.json").read_text(encoding="utf-8"))

parts: list[str] = []


def rows_of(font, cp):
    return next((g["rows"] for g in font["glyphs"] if g["codepoint"] == cp), None)


def glyph(font, cp, ox, oy, scale, colour="#fff"):
    cw = font["cell_width"]
    rows = rows_of(font, cp)
    if rows:
        for ry, bits in enumerate(rows):
            for rx in range(cw):
                if bits & (1 << (cw - 1 - rx)):
                    parts.append(
                        f'<rect x="{ox + rx * scale:.0f}" y="{oy + ry * scale:.0f}" '
                        f'width="{scale}" height="{scale}" fill="{colour}"/>'
                    )
    return font["advance"] * scale


def text(font, s, ox, oy, scale, colour="#fff"):
    x = ox
    for ch in s:
        x += glyph(font, ord(ch), x, oy, scale, colour)
    return x - ox


def label(s, x, y, size=11, colour="#7d8590", anchor="start", weight="normal"):
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    parts.append(
        f'<text x="{x:.0f}" y="{y:.0f}" font-family="ui-monospace,Consolas,monospace" '
        f'font-size="{size}" font-weight="{weight}" fill="{colour}" '
        f'text-anchor="{anchor}">{s}</text>'
    )


W = 1000
y = 36
label("Font comparison at matched footprint — 12 px per character in both columns",
      24, y, 16, "#e6edf3", weight="bold")
y += 20
label("left: font 0, 3x5 at scale 3 (advance 4)    right: candidate font 1, 5x7 at scale 2 (advance 6)",
      24, y, 12)
y += 26

COL_L, COL_R = 40, 540
parts.append(f'<rect x="24" y="{y}" width="480" height="34" fill="#161b22"/>')
parts.append(f'<rect x="524" y="{y}" width="452" height="34" fill="#161b22"/>')
label("3x5  —  shipping today", COL_L, y + 22, 13, "#8b949e", weight="bold")
label("5x7  —  candidate, not wired up", COL_R, y + 22, 13, "#8b949e", weight="bold")
y += 48

SAMPLES = [
    "Timer",
    "Navigation",
    "Buzz Phone",
    "Notifications",
    "abcdefghijklm",
    "nopqrstuvwxyz",
    "0.1.0-m16 Aug 05",
    "Sync { Fail | Fav ~",
]
for s in SAMPLES:
    text(F0, s, COL_L, y, 3)
    text(F1, s, COL_R, y, 2)
    y += 26

y += 16
label("Lowercase, magnified 6x — where the 3x5 gives up", 24, y, 14, "#e6edf3", weight="bold")
y += 14
for s in ["aeos", "mnw"]:
    y += 8
    text(F0, s, COL_L, y, 6)
    text(F1, s, COL_R, y, 5)
    y += 46

y += 20
label("Launcher row at 1:1 — 224 x 64 button, name centred", 24, y, 14, "#e6edf3", weight="bold")
y += 16
for i, (font, scale, tag) in enumerate([(F0, 3, "3x5 @3"), (F1, 2, "5x7 @2")]):
    bx = 24 + i * 500
    parts.append(f'<rect x="{bx}" y="{y}" width="240" height="80" fill="#000" stroke="#30363d"/>')
    parts.append(f'<rect x="{bx + 8}" y="{y + 8}" width="224" height="64" rx="8" '
                 f'fill="#0b3d0b" stroke="#1f7a1f"/>')
    name = "Navigation"
    w = font["advance"] * scale * len(name)
    text(font, name, bx + 120 - w / 2, y + 8 + (64 - font["cell_height"] * scale) / 2, scale)
    label(tag, bx + 250, y + 44, 12)
y += 104

H = y + 20
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
       f'viewBox="0 0 {W} {H}"><rect width="{W}" height="{H}" fill="#010409"/>'
       + "".join(parts) + "</svg>")
OUT_SVG.parent.mkdir(parents=True, exist_ok=True)
OUT_SVG.write_text(svg, encoding="utf-8")
print(f"wrote {OUT_SVG}")
