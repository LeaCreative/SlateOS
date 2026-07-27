# Shared Slate assets

Single origin for wire constants and built-in font data consumed by firmware, Kotlin DSL,
and (later) the JS builder.

- `sdp_wire.json` — SDP opcodes, enumerations, and flag bits (normative: roadmap §4.3 / §4.5).
- `fonts/font0_3x5.json` — built-in font 0 metrics and glyph bitmaps.

Regenerate language bindings:

```powershell
python tools/codegen/generate.py
```

Firmware `include/sdp_opcodes.hpp` remains the C++ wire header; `generate.py` verifies it
matches `sdp_wire.json`. Font output is embedded via `include/font_builtin.hpp`.
