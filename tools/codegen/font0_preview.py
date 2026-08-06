#!/usr/bin/env python3
"""
Render shared/fonts/font0_3x5.json to an SVG proof sheet.

The watch is sealed and a flash costs the operator real time, so a font change
gets looked at here first. Pixels are drawn as squares at the same scale the
renderer uses (scale N = N x N block per font pixel), white on black, matching
the panel.
"""

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
FONT_JSON = ROOT / "shared" / "fonts" / "font0_3x5.json"
OUT_SVG = ROOT / "build" / "font0-preview.svg"

font = json.loads(FONT_JSON.read_text(encoding="utf-8"))
CW, CH = font["cell_width"], font["cell_height"]
ADV = font["advance"]
FIRST = font["first_codepoint"]
ROWS = {g["codepoint"]: g["rows"] for g in font["glyphs"]}

parts: list[str] = []


def glyph(cp: int, ox: float, oy: float, scale: int, colour: str = "#fff") -> float:
    """Draw one glyph, return the advance in px."""
    rows = ROWS.get(cp)
    if rows is not None:
        for ry, bits in enumerate(rows):
            for rx in range(CW):
                if bits & (1 << (CW - 1 - rx)):
                    parts.append(
                        f'<rect x="{ox + rx * scale:.0f}" y="{oy + ry * scale:.0f}" '
                        f'width="{scale}" height="{scale}" fill="{colour}"/>'
                    )
    return ADV * scale


def text(s: str, ox: float, oy: float, scale: int, colour: str = "#fff") -> float:
    x = ox
    for ch in s:
        x += glyph(ord(ch), x, oy, scale, colour)
    return x - ox


def label(s: str, x: float, y: float, size: int = 11, colour: str = "#7d8590",
          anchor: str = "start") -> None:
    parts.append(
        f'<text x="{x:.0f}" y="{y:.0f}" font-family="ui-monospace,Consolas,monospace" '
        f'font-size="{size}" fill="{colour}" text-anchor="{anchor}">'
        f'{s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")}</text>'
    )


W = 900
y = 0

# ── Proof sheet: every glyph at scale 3, the launcher's text size ────────────
y += 34
label("Built-in font 0 — every glyph at scale 3 (9x15 px), as the watch draws it", 24, y, 15, "#e6edf3")
y += 10

COLS = 16
CELL_BOX_W = 52
CELL_BOX_H = 60
grid_top = y + 14
codepoints = sorted(ROWS)
for i, cp in enumerate(codepoints):
    col, row = i % COLS, i // COLS
    bx = 24 + col * CELL_BOX_W
    by = grid_top + row * CELL_BOX_H
    parts.append(
        f'<rect x="{bx}" y="{by}" width="{CELL_BOX_W - 6}" height="{CELL_BOX_H - 14}" '
        f'rx="3" fill="#0d1117" stroke="#21262d"/>'
    )
    glyph(cp, bx + 12, by + 10, 3)
    shown = {32: "spc"}.get(cp, chr(cp))
    label(shown, bx + (CELL_BOX_W - 6) / 2, by + CELL_BOX_H - 3, 10, "#7d8590", "middle")

y = grid_top + ((len(codepoints) + COLS - 1) // COLS) * CELL_BOX_H + 26

# ── The four pictograms, called out large ───────────────────────────────────
label("Pictograms — scale 3, 5 and 8", 24, y, 15, "#e6edf3")
y += 22
picto = font.get("pictograms", {})
px = 24
for name, ch in picto.items():
    parts.append(f'<rect x="{px}" y="{y}" width="200" height="76" rx="5" '
                 f'fill="#0d1117" stroke="#21262d"/>')
    glyph(ord(ch), px + 16, y + 26, 3)
    glyph(ord(ch), px + 52, y + 18, 5)
    glyph(ord(ch), px + 104, y + 8, 8)
    label(f"{name}  '{ch}'", px + 16, y + 70, 10, "#7d8590")
    px += 212
y += 104

# ── Real strings, at the sizes they will actually be used ───────────────────
label("Strings at the sizes they are actually used", 24, y, 15, "#e6edf3")
y += 24

samples = [
    ("Launcher row label, scale 3", "Navigation", 3),
    ("Launcher row label, scale 3", "Buzz Phone", 3),
    ("Longest bundled name, scale 3", "Notifications", 3),
    ("Version line, scale 2 (was all boxes)", "0.1.0-m16 Aug 05 20:56", 2),
    ("Mixed case + pictograms, scale 3", "Paired { Sync | Fav ~", 3),
    ("Clock face digits, scale 8", "21:29", 8),
]
for caption, s, scale in samples:
    label(caption, 24, y + 11, 11)
    w = text(s, 300, y, scale)
    label(f"{w:.0f} px", 880, y + 11, 10, "#7d8590", "end")
    y += max(CH * scale, 16) + 16

# ── A 240x240 mock of the launcher itself ───────────────────────────────────
y += 10
label("Launcher at 1:1 — 240x240 panel, 3 rows of 72 px", 24, y, 15, "#e6edf3")
y += 18
panel_x, panel_y = 24, y
parts.append(f'<rect x="{panel_x}" y="{panel_y}" width="240" height="240" fill="#000" stroke="#30363d"/>')
text("Apps", panel_x + 8, panel_y + 6, 2, "#8b949e")
rows_demo = ["Timer", "Navigation", "Camera"]
for i, name in enumerate(rows_demo):
    ry = panel_y + 24 + i * 72
    parts.append(f'<rect x="{panel_x + 8}" y="{ry}" width="224" height="64" rx="8" '
                 f'fill="#0b3d0b" stroke="#1f7a1f"/>')
    w = ADV * 3 * len(name)
    text(name, panel_x + 120 - w / 2, ry + 24, 3)
label("row 4 (Buzz Phone) is one flick below", panel_x + 260, panel_y + 130, 12, "#7d8590")

H = panel_y + 240 + 40
svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
    f'viewBox="0 0 {W} {H}">'
    f'<rect width="{W}" height="{H}" fill="#010409"/>'
    + "".join(parts)
    + "</svg>"
)
OUT_SVG.parent.mkdir(parents=True, exist_ok=True)
OUT_SVG.write_text(svg, encoding="utf-8")
print(f"wrote {OUT_SVG}")
