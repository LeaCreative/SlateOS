# Slate companion (M4)

Kotlin DSL, desktop emulator, and golden-file tests for SDP display lists.

## Prerequisites

- JDK 17+
- Run `python tools/codegen/generate.py` from the repo root after changing `shared/`.

## Build & test

```powershell
cd companion
.\gradlew.bat :sdp-tests:generateGoldens
.\gradlew.bat test
```

## Run emulator

```powershell
.\gradlew.bat :sdp-emulator:run
```

## Shared assets

Wire constants and font 0 live in `../shared/`. Firmware consumes the same font JSON via
`include/font_builtin.hpp`; Kotlin bindings are generated into `sdp-core`.

## Font design

`shared/fonts/font0_3x5.json` is the single glyph source. `tools/codegen/generate.py`
embeds it into firmware (`font_builtin.hpp`) and Kotlin (`FontBuiltin3x5.kt`). Text
layout (`TextLayout` / firmware `draw_text_run`) reads metrics from that generated data
so emulator and watch cannot drift on cell size, advance, or bitmaps.
