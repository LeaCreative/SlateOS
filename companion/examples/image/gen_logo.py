#!/usr/bin/env python3
"""Downscale logo.png to an RGB332 blob and rewrite main.js.

SDP kMaxListBytes is 4096; a single PATCH + CLEAR + COMMIT leaves room for
exactly 63x63 RGB332 pixels. Re-run after replacing logo.png.

**This script rewrites the whole of main.js, header comment included.** Edit
the header here, not there, or the next run silently reverts it — that header
is required by docs/subapp-rules.md §4 and the budget figures in it are
computed below rather than typed, so they stay true if W/H change.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
SRC = HERE / "logo.png"
W = H = 63

# Display-list arithmetic, so the generated header states measured numbers for
# whatever size this run produced rather than numbers that were true once.
#   SET_PALETTE 4 + CLEAR 2 + PATCH header 10 + COMMIT 2
LIST_OVERHEAD_BYTES = 18
HARD_CAP_BYTES = 4096       # sdp::kMaxListBytes — the parser rejects above this
PRACTICAL_LIMIT_BYTES = 2048  # docs/subapp-rules.md §2 — the credit window (§3)


def list_bytes(w: int, h: int) -> int:
    return LIST_OVERHEAD_BYTES + w * h


def largest_side_within(limit: int) -> int:
    """Biggest square whose whole display list fits `limit`."""
    side = 1
    while list_bytes(side + 1, side + 1) <= limit:
        side += 1
    return side


def rgb332(r: int, g: int, b: int) -> int:
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)


def pixels_from_png(path: Path) -> list[int]:
    img = Image.open(path).convert("RGBA").resize((W, H), Image.Resampling.LANCZOS)
    out: list[int] = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = img.getpixel((x, y))
            if a < 16:
                r = g = b = 0
            else:
                r = (r * a) // 255
                g = (g * a) // 255
                b = (b * a) // 255
            out.append(rgb332(r, g, b))
    return out


def logo_js(pixels: list[int]) -> str:
    rows: list[str] = []
    for i in range(0, len(pixels), 16):
        chunk = pixels[i : i + 16]
        comma = "," if i + 16 < len(pixels) else ""
        rows.append("    " + ", ".join("0x%02x" % p for p in chunk) + comma)
    return "\n".join(
        [
            "  // Generated RGB332 %dx%d (%d bytes) -- fits SDP kMaxListBytes."
            % (W, H, len(pixels)),
            "  var LOGO_W = %d;" % W,
            "  var LOGO_H = %d;" % H,
            "  var LOGO = [",
            *rows,
            "  ];",
        ]
    )


def budget_note() -> str:
    """The §4.5 budget paragraph, sized to what this run actually emitted."""
    total = list_bytes(W, H)
    fits = largest_side_within(PRACTICAL_LIMIT_BYTES)
    if total <= PRACTICAL_LIMIT_BYTES:
        verdict = [
            " * Budget: %d B display list, 4 ops, 2 of them drawing. Inside both the"
            % total,
            " *        %d B parser cap (kMaxListBytes) and the %d B practical limit"
            % (HARD_CAP_BYTES, PRACTICAL_LIMIT_BYTES),
            " *        in docs/subapp-rules.md §2, so it is not exposed to the credit",
            " *        window in §3.",
        ]
    else:
        verdict = [
            " * Budget: **%d B display list — over the %d B practical limit in"
            % (total, PRACTICAL_LIMIT_BYTES),
            " *        docs/subapp-rules.md §2.** 4 ops, 2 of them drawing.",
            " *",
            " *        It clears the %d B parser cap (kMaxListBytes), so the watch"
            % HARD_CAP_BYTES,
            " *        accepts it. What it does not clear is the credit window (§3):",
            " *        the watch advertises 4096-or-0 rather than real free bytes, so",
            " *        a screen this large lands or is dropped depending on what",
            " *        happened before it. That is what makes this app fail",
            " *        *intermittently* rather than never, and it is the reason N-46",
            " *        was found. Look for the companion's",
            " *        `pushToWatch DROPPED: slate.image, %d B` line before" % total,
            " *        suspecting the script.",
            " *",
            " *        Cheap in render cost, unlike examples/image-vector: one PATCH",
            " *        walked 30 times is nothing. The whole cost here is bytes.",
            " *",
            " *        PROPOSAL, not applied — the artwork is the operator's. The",
            " *        list is %d B of headers plus one byte per pixel, so the largest"
            % LIST_OVERHEAD_BYTES,
            " *        square that fits the practical limit is **%dx%d = %d B**."
            % (fits, fits, list_bytes(fits, fits)),
            " *        (%dx%d is %d B and misses.) That is W/H in gen_logo.py and a"
            % (fits + 1, fits + 1, list_bytes(fits + 1, fits + 1)),
            " *        regenerated array; it has not been changed because it visibly",
            " *        shrinks the logo and that is the operator's call.",
        ]
    return "\n".join(verdict)


MAIN_TMPL = """/**
 * Image -- shows the Slate logo on the watch via a single PATCH (RGB332).
 *
 * Draws: black CLEAR, then one {w}x{h} RGB332 bitmap centred at ({ox},{oy}) on
 *        the 240x240 face. No text, no elements, no chrome.
 * Does:  nothing but draw. onFocus logs and pushes the screen, render()
 *        repeats it, BACK (0x06) relinquishes focus. No adapters, no timers,
 *        no persistence, no input beyond BACK.
 * Perms: none, and none are needed. The app uses slate.ui and slate.log, and
 *        neither is permission-gated.
 * Settings: none. Nothing here is user-tunable — the bitmap is generated
 *        artwork, not a preference.
{budget}
 *
 * Install: zip manifest.json + main.js and open the zip on the phone
 * (SideloadActivity). Regenerate the bitmap with `python gen_logo.py`, which
 * rewrites this file including this header.
 *
 * Downloaded JavaScript only — never dex/JAR/.so. The pixel array below is
 * data, and the watch never executes anything it receives over BLE.
 */
(function (global) {{
  'use strict';

{logo}
  var PATCH_RGB332 = 1;
  var PATCH_RAW = 0;
  // Centre on the 240x240 panel.
  var ORIGIN_X = ((240 - LOGO_W) / 2) | 0;
  var ORIGIN_Y = ((240 - LOGO_H) / 2) | 0;

  function face() {{
    return slate.ui.displayList(function (b) {{
      b.palette(0, 0x0000);
      b.clear(slate.PAL(0));
      b.patch(0, ORIGIN_X, ORIGIN_Y, LOGO_W, LOGO_H, PATCH_RGB332, PATCH_RAW, LOGO);
      b.commit();
    }});
  }}

  global.onFocus = function () {{
    slate.log('info', 'image: show logo ' + LOGO_W + 'x' + LOGO_H);
    return face();
  }};

  global.render = function () {{
    return face();
  }};

  global.onBlur = function () {{
    return [];
  }};

  global.onInput = function (ev) {{
    // Swipe right -- same relinquish as the other demos.
    if (ev.op === 0x06) {{
      return [{{ type: 'relinquishFocus' }}, {{ type: 'inputHandled' }}];
    }}
    return [{{ type: 'inputUnhandled' }}];
  }};

  global.onEvent = function () {{
    return [];
  }};
}})(typeof globalThis !== 'undefined' ? globalThis : this);
"""


def main() -> None:
    if not SRC.is_file():
        raise SystemExit(f"missing {SRC}")
    pixels = pixels_from_png(SRC)
    (HERE / f"logo_{W}.rgb332").write_bytes(bytes(pixels))
    (HERE / "main.js").write_text(
        MAIN_TMPL.format(
            logo=logo_js(pixels),
            budget=budget_note(),
            w=W,
            h=H,
            ox=(240 - W) // 2,
            oy=(240 - H) // 2,
        ),
        # UTF-8, not ascii: the §4 header cites docs/subapp-rules.md sections
        # and every other sub-app source in examples/ is already UTF-8.
        # InstalledStore.entryJs() and ScriptResources.read() both decode UTF-8.
        encoding="utf-8",
        newline="\n",
    )
    total = list_bytes(W, H)
    over = " — OVER the %d B practical limit" % PRACTICAL_LIMIT_BYTES
    print(
        f"wrote main.js ({W}x{H}, {len(pixels)} px, {total} B display list"
        f"{over if total > PRACTICAL_LIMIT_BYTES else ''})"
    )


if __name__ == "__main__":
    main()
