# Asset packs + LittleFS (M11)

Fonts and icon atlases live as `.slap` files in LittleFS on the XT25F32B (4 MB SPI
NOR, CS=P0.05). The NOR **shares SPI0** with the ST7789 — every flash op goes through
`spi::acquire()` / `release()`.

## LittleFS + deep power-down

- Driver: `xt25.hpp` — read / page-program / 4 KB sector erase / JEDEC probe.
- Idle: `deep_power_down()` = CS high → CS low → `0xB9` → CS high; `wake()` uses `0xAB`.
- FS: vendored LittleFS v2.9.3 under `third_party/littlefs`, port in `lfs_fs.cpp`.
- After idle UI work, call `fs::sleep_flash()`.

## Pack format (`.slap`)

```
magic 'SLAP' | version:u16 | flags:u16 | content_hash:u32 | index_off:u32 | index_len:u32
… glyph/icon payloads …
index: TLV records (type:u8, id:u8, length:u16, body[length])
```

- `content_hash` = FNV-1a of bytes from offset 12 to EOF.
- Record types: `0x01` FONT, `0x02` ATLAS, `0xF0` END; **unknown types skipped by length**.
- Index carries absolute file offsets — no need to load the whole pack to resolve one glyph.

Parser: `slate::pack::parse` — same reject-not-fault discipline as the display-list parser.
Fuzz target: `tests/fuzz/asset_pack_fuzz.cpp`.

## Glyph cache (6 KB)

`slate::cache::GlyphCache` — LRU over individual glyphs/icons paged via a read hook.
Host test prints hit rate on a digit-heavy string (measured **100.00%** on the
cached pass of `0123…` ×10 with font 0 in `core.slap`).

## ASSET channel (3)

Ops: `BEGIN` / `CHUNK` / `ABORT` / `COMMIT` (phone→watch); `CREDIT` / `ACK` / `NAK` (watch→phone).

- Windowed credit (`kWindowBytes=1024`).
- Resumable: ACK carries `received` offset; out-of-order CHUNK re-ACKs.
- Hash-verified on COMMIT (FNV of staging bytes[12..)).
- `set_yield_busy(true)` caps CHUNK processing to 256 B/quantum so DISPLAY/INPUT win.

## Desktop tool

```powershell
python tools/assetpack/pack.py --out shared/generated/core.slap --emit-ids shared/generated/asset_ids.json
```

Inputs: font JSON (`shared/fonts/…`), icon atlas JSON (`shared/icons/…`), optional TTF (+fontTools/Pillow).
Outputs: `.slap` + `asset_ids.hpp` / `asset_ids.json` + Kotlin `AssetIds.kt`.

## Host tests

```powershell
python tools/assetpack/pack.py --out shared/generated/core.slap --emit-ids shared/generated/asset_ids.json
cmake -S tests/host -B build/host-tests
.\scripts\run_host_tests.cmd assets
```
