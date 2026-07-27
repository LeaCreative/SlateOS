# Shared Slate assets

Single origin for wire constants, fonts, and icon atlases consumed by firmware, Kotlin DSL,
and (later) the JS builder.

- `sdp_wire.json` — SDP opcodes, enumerations, and flag bits (normative: roadmap §4.3 / §4.5).
- `fonts/font0_3x5.json` — built-in font 0 metrics and glyph bitmaps.
- `icons/cat16.json` — category icon atlas (M11).
- `generated/` — `.slap` packs + `asset_ids.*` from `tools/assetpack/pack.py`.

Regenerate language bindings:

```powershell
python tools/codegen/generate.py
python tools/assetpack/pack.py --out shared/generated/core.slap --emit-ids shared/generated/asset_ids.json
```

See [docs/assets.md](../docs/assets.md).
