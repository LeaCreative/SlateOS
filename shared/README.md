# Shared Slate assets

Single origin for wire constants, fonts, and icon atlases consumed by firmware, Kotlin DSL,
and (later) the JS builder.

- `sdp_wire.json` — SDP opcodes, enumerations, and flag bits (normative: roadmap §4.3 / §4.5).
- `fonts/font0_3x5.json` — built-in font 0 metrics and glyph bitmaps. Covers
  codepoints 45–58 (`-` `.` `/` `0`–`9` `:`); anything else draws as a filled
  cell. At 3×5 pixels it is unreadable at 1:1 on the 240 px panel, so on-watch
  text goes through `TEXT_SCALED` (opcode `0xE0`), which blows each glyph pixel
  up into a `scale`×`scale` block. The local watch face uses scale 8 for the
  time and 3 for everything else.
- `icons/cat16.json` — category icon atlas (M11).
- `generated/` — `.slap` packs + `asset_ids.*` from `tools/assetpack/pack.py`.

Regenerate language bindings:

```powershell
python tools/codegen/generate.py
python tools/assetpack/pack.py --out shared/generated/core.slap --emit-ids shared/generated/asset_ids.json
```

See [docs/assets.md](../docs/assets.md).
