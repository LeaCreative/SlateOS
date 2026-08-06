#!/usr/bin/env python3
"""
Source of truth for built-in font 0, as reviewable ASCII art.

Emits shared/fonts/font0_3x5.json, which tools/codegen/generate.py then turns
into include/font_builtin.hpp (firmware) and generated/FontBuiltin3x5.kt
(companion). Both sides come from this one file, so the byte-identical
Kotlin/JS golden-file rule holds automatically.

Why art and not numbers: the old JSON stored rows as decimals (7, 5, 6 ...).
That is unreviewable — nobody can see a wrong glyph in a diff of integers, and
a 3x5 cell has no margin for an undetected mistake. Here a bad glyph is
visible on the page.

Cell is 3 wide by 5 tall. '#' is on, anything else is off. Row bits are
MSB-left: '#..' = 4, '.#.' = 2, '..#' = 1, '###' = 7.

Codepoints are printable ASCII 32..126 so a label is just a Kotlin/JS string.
The four pictograms take the four slots nobody puts in a watch label:

    {  tick        |  cross        }  smiley        ~  heart

Do not hand-write those in UI code — use the Glyph constants the generator
emits, so a future remap is one edit here.
"""

import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT_JSON = ROOT / "shared" / "fonts" / "font0_3x5.json"

CELL_W, CELL_H = 3, 5

# ── Glyph art ────────────────────────────────────────────────────────────────
# Uppercase fills all five rows. Lowercase sits on rows 1-4 with ascenders
# reaching row 0; there is no descender row, so g/j/p/q/y fake theirs on row 4.
ART: "dict[str, list[str]]" = {
    " ": ["...", "...", "...", "...", "..."],
    "!": [".#.", ".#.", ".#.", "...", ".#."],
    '"': ["#.#", "#.#", "...", "...", "..."],
    "#": ["#.#", "###", "#.#", "###", "#.#"],
    "$": [".#.", ".##", ".#.", "##.", ".#."],
    "%": ["#.#", "..#", ".#.", "#..", "#.#"],
    "&": [".#.", "#.#", ".#.", "#.#", ".##"],
    "'": [".#.", ".#.", "...", "...", "..."],
    "(": ["..#", ".#.", ".#.", ".#.", "..#"],
    ")": ["#..", ".#.", ".#.", ".#.", "#.."],
    "*": ["...", "#.#", ".#.", "#.#", "..."],
    "+": ["...", ".#.", "###", ".#.", "..."],
    ",": ["...", "...", "...", ".#.", "#.."],
    "-": ["...", "...", "###", "...", "..."],
    ".": ["...", "...", "...", "...", ".#."],
    "/": ["..#", "..#", ".#.", "#..", "#.."],
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "1": [".#.", ".#.", ".#.", ".#.", ".#."],
    "2": ["###", "..#", "###", "#..", "###"],
    "3": ["###", "..#", "###", "..#", "###"],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
    "5": ["###", "#..", "###", "..#", "###"],
    "6": ["###", "#..", "###", "#.#", "###"],
    "7": ["###", "..#", "..#", "..#", "..#"],
    "8": ["###", "#.#", "###", "#.#", "###"],
    "9": ["###", "#.#", "###", "..#", "###"],
    ":": ["...", ".#.", "...", ".#.", "..."],
    ";": ["...", ".#.", "...", ".#.", "#.."],
    "<": ["..#", ".#.", "#..", ".#.", "..#"],
    "=": ["...", "###", "...", "###", "..."],
    ">": ["#..", ".#.", "..#", ".#.", "#.."],
    "?": ["##.", "..#", ".#.", "...", ".#."],
    "@": [".#.", "#.#", "###", "#..", ".##"],

    "A": [".#.", "#.#", "###", "#.#", "#.#"],
    "B": ["##.", "#.#", "##.", "#.#", "##."],
    "C": [".##", "#..", "#..", "#..", ".##"],
    "D": ["##.", "#.#", "#.#", "#.#", "##."],
    "E": ["###", "#..", "##.", "#..", "###"],
    "F": ["###", "#..", "##.", "#..", "#.."],
    "G": [".##", "#..", "#.#", "#.#", ".##"],
    "H": ["#.#", "#.#", "###", "#.#", "#.#"],
    "I": ["###", ".#.", ".#.", ".#.", "###"],
    "J": ["..#", "..#", "..#", "#.#", ".#."],
    "K": ["#.#", "#.#", "##.", "#.#", "#.#"],
    "L": ["#..", "#..", "#..", "#..", "###"],
    "M": ["#.#", "###", "###", "#.#", "#.#"],
    "N": ["#.#", "###", "#.#", "#.#", "#.#"],
    "O": [".#.", "#.#", "#.#", "#.#", ".#."],
    "P": ["##.", "#.#", "##.", "#..", "#.."],
    "Q": [".#.", "#.#", "#.#", "##.", ".##"],
    "R": ["##.", "#.#", "##.", "#.#", "#.#"],
    "S": [".##", "#..", ".#.", "..#", "##."],
    "T": ["###", ".#.", ".#.", ".#.", ".#."],
    "U": ["#.#", "#.#", "#.#", "#.#", "###"],
    "V": ["#.#", "#.#", "#.#", "#.#", ".#."],
    "W": ["#.#", "#.#", "###", "###", "#.#"],
    "X": ["#.#", "#.#", ".#.", "#.#", "#.#"],
    "Y": ["#.#", "#.#", ".#.", ".#.", ".#."],
    "Z": ["###", "..#", ".#.", "#..", "###"],

    "[": [".##", ".#.", ".#.", ".#.", ".##"],
    "\\": ["#..", "#..", ".#.", "..#", "..#"],
    "]": ["##.", ".#.", ".#.", ".#.", "##."],
    "^": [".#.", "#.#", "...", "...", "..."],
    "_": ["...", "...", "...", "...", "###"],
    "`": ["#..", ".#.", "...", "...", "..."],

    "a": ["...", ".##", "#.#", "#.#", ".##"],
    "b": ["#..", "#..", "##.", "#.#", "##."],
    "c": ["...", ".##", "#..", "#..", ".##"],
    "d": ["..#", "..#", ".##", "#.#", ".##"],
    "e": ["...", ".#.", "#.#", "##.", ".##"],
    "f": [".##", "#..", "##.", "#..", "#.."],
    "g": ["...", ".##", "#.#", ".##", "##."],
    "h": ["#..", "#..", "##.", "#.#", "#.#"],
    "i": [".#.", "...", ".#.", ".#.", ".#."],
    "j": ["..#", "...", "..#", "#.#", ".#."],
    "k": ["#..", "#.#", "##.", "##.", "#.#"],
    "l": ["##.", ".#.", ".#.", ".#.", ".##"],
    "m": ["...", "###", "###", "#.#", "#.#"],
    "n": ["...", "##.", "#.#", "#.#", "#.#"],
    "o": ["...", ".#.", "#.#", "#.#", ".#."],
    "p": ["...", "##.", "#.#", "##.", "#.."],
    "q": ["...", ".##", "#.#", ".##", "..#"],
    "r": ["...", "#.#", "##.", "#..", "#.."],
    "s": ["...", ".##", "##.", "..#", "##."],
    "t": [".#.", "###", ".#.", ".#.", ".##"],
    "u": ["...", "#.#", "#.#", "#.#", ".##"],
    "v": ["...", "#.#", "#.#", "#.#", ".#."],
    "w": ["...", "#.#", "#.#", "###", "###"],
    "x": ["...", "#.#", ".#.", ".#.", "#.#"],
    "y": ["...", "#.#", "#.#", ".##", "##."],
    "z": ["...", "###", "..#", ".#.", "###"],

    # Pictograms. Three pixels of width is not much of a canvas — these are
    # meant to read at scale 3 and up, next to a word, not on their own.
    "{": ["..#", "..#", "#.#", ".#.", "..."],   # tick
    "|": ["...", "#.#", ".#.", "#.#", "..."],   # cross
    "}": ["...", "#.#", "...", "#.#", ".#."],   # smiley
    "~": ["#.#", "###", "###", ".#.", "..."],   # heart
}

# Named slots for the pictograms, emitted as constants by generate.py so no UI
# code ever hard-codes the punctuation character.
PICTOGRAMS = {"TICK": "{", "CROSS": "|", "SMILEY": "}", "HEART": "~"}

FIRST_CP = 32
LAST_CP = 126


def row_bits(row: str) -> int:
    if len(row) != CELL_W:
        raise ValueError(f"row {row!r} is not {CELL_W} wide")
    value = 0
    for i, ch in enumerate(row):
        if ch == "#":
            value |= 1 << (CELL_W - 1 - i)
        elif ch != ".":
            raise ValueError(f"row {row!r} may only contain '#' and '.'")
    return value


def build() -> dict:
    glyphs = []
    for cp in range(FIRST_CP, LAST_CP + 1):
        ch = chr(cp)
        art = ART.get(ch)
        if art is None:
            raise SystemExit(f"missing glyph art for {ch!r} (codepoint {cp})")
        if len(art) != CELL_H:
            raise SystemExit(f"glyph {ch!r} has {len(art)} rows, expected {CELL_H}")
        glyphs.append({"codepoint": cp, "rows": [row_bits(r) for r in art]})
    return {
        "id": 0,
        "name": "builtin_3x5",
        "cell_width": CELL_W,
        "cell_height": CELL_H,
        "advance": 4,
        "first_codepoint": FIRST_CP,
        "glyph_count": len(glyphs),
        "pictograms": PICTOGRAMS,
        "glyphs": glyphs,
    }


if __name__ == "__main__":
    font = build()
    OUT_JSON.write_text(json.dumps(font, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUT_JSON} — {font['glyph_count']} glyphs, "
          f"{font['glyph_count'] * CELL_H} bytes of table")
