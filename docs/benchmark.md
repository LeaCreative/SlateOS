# Slate measurement gates (throughput / latency / render)

Harness for roadmap **gates A, B, D** before freezing SDP. Gate C (power) is a
separate PPK II run (§8).

## Protocol (channel 7 DIAG — debug firmware only)

| Op | Direction | Payload |
|---|---|---|
| `0x01` PING_REQ | phone→watch | opaque token |
| `0x81` PING_RSP | watch→phone | echo token |
| `0x02` THRU_START | phone→watch | `u32 LE` expected payload bytes + optional first chunk |
| `0x03` THRU_DATA | phone→watch | payload bytes (counted toward N) |
| `0x82` THRU_RESULT | watch→phone | `u32 elapsed_us`, `u32 bytes`, `u32 kbps×100`, `u8 status` |
| `0x04` RTT_REQ | phone→watch | token (typically `u64` phone ns) |
| `0x84` RTT_RSP | watch→phone | token + `u32` watch `micros()` |
| `0x05` MBUF_REQ | phone→watch | empty |
| `0x85` MBUF_RSP | watch→phone | peak_used, block_count, block_size, free_now (`u16` each) |
| `0x06` RENDER_REQ | phone→watch | display-list bytes |
| `0x86` RENDER_RSP | watch→phone | `u32 parse_us`, `u32 render_us`, `u32 ops`, `u8 status` |

Firmware: `include/sdp_diag.hpp`, wired from `ble::Link` when `SLATE_BLE_DIAG=1`.
Android: `slate.diag.SdpDiag` + `BenchmarkActivity`.

Release builds reject channels 6–7; do not advertise DIAG in negotiation flags.

## How to run

### Firmware (host tests — no radio)

```powershell
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests
ctest --test-dir build/host-tests -R sdp_diag -V
```

### On device (needs Slate debug image with NimBLE on the watch)

1. Flash Debug firmware (`SLATE_BLE_DIAG=1`, ideally `SLATE_HAS_NIMBLE=1`).
2. Phone: Associate → Start link → **Benchmarks (gates A / B / D)**.
3. Run all, or A / B / D individually.
4. Record MTU / PHY / interval shown in the report (refuse cases matter).

Default A transfer is **128 KB**. RTT is **1000 samples**. Render is **100×** an ~80-byte list.

## Interpreting results

Always read numbers **together with** negotiated link params. A “fail” at MTU 23 / 1M PHY
is a radio negotiation problem first; a fail at MTU 247 + DLE + 2M is a protocol problem.

### Gate A — Sustained throughput ≥ 60 kB/s

Use the **watch** `kbps` from `THRU_RESULT` (wall clock from first THRU_START to N bytes
received). Phone-side kB/s is a sanity check (includes TX queue + notify latency).

| Observation | Likely cause | Protocol / design change |
|---|---|---|
| < ~27 kB/s, MTU≈23 | No ATT MTU exchange | Keep requiring MTU 247 in negotiation; abort “streaming” profile if refused |
| ~50–55 kB/s, DLE off (27-octet LL) | No Data Length Extension | **Hard requirement**: DLE 251/251 before any patch/camera tier. Do not shrink SDP for this — fix link setup |
| 60–90 kB/s but mbuf peak == block_count | MSYS exhausted under burst | Raise `MSYS_1_BLOCK_COUNT` (RAM) **or** add **credit / ACK_REQ pacing** on DISPLAY+ASSET so the phone stops flooding Write Without Response; optional smaller max message |
| Still <60 kB/s at 247+DLE+2M | Framing / app overhead | Prefer **fewer larger messages** over many tiny ones; avoid per-primitive round trips; keep patch payloads on ASSET with coalescing. If still short: **drop the streaming / camera tier** from the protocol promise and lean on `IMAGE` / richer opcodes (roadmap §10) |
| Watch kB/s OK, phone kB/s much lower | Notify back-pressure / result delay | Not a throughput fail for gate A; fix TX notify queue if RTT also suffers |

**Specific freeze decision:** if A fails on target handsets after negotiation is correct,
**do not ship a pixel-streaming patch tier**. Keep display-list tier; expand `IMAGE` /
atlas opcodes rather than raw framebuffer patches.

### Gate B — Tap-to-response p95 < 250 ms

Measured as phone wall RTT on DIAG (`RTT_REQ`→`RTT_RSP`). This is the **link floor**.
Real tap→UI is floor + phone JS + DISPLAY push. If DIAG p95 already ≥250 ms, interaction
cannot be phone-driven.

| Observation | Likely cause | Protocol / design change |
|---|---|---|
| p95 high, interval ≥30–50 ms | Slow connection interval | Session profile **must** request Active (~15–30 ms) for interactive screens; Ambient must not be used during tap targets |
| p95 ≈ 2×interval + small | One-shot request/response | **Move interaction state to the watch** (already planned M7): local scroll, focus, press highlight, haptic — **zero RTT** for those. Phone only gets semantic events |
| p95 >> interval, spikes | Scheduling / FGS / WWAN coexist | Prefer Write Without Response + notify; avoid synchronous GATT reads on the input path; keep DIAG out of release |
| mean OK, p99 horrible | Coexistence / scan duty | Document handset variance; design UI for p95 not mean; never block UI on p99 |

**Specific freeze decision:** if B fails, **enlarge the watch-owned state set** before
freezing §4.4/§4.6: scroll position, selection, back stack within a pushed screen, and
optimistic visuals. Do **not** add more chatty phone round-trips to “fix” latency.

### Gate D — Parse + render of ~80-byte list < 30 ms

Uses watch `parse_us + render_us` from `RENDER_RSP` (not phone RTT).

| Observation | Likely cause | Protocol / design change |
|---|---|---|
| parse alone high | Validator / opcode walk | Cap ops/bytes (already); avoid nested explosion; keep extension opcodes skippable |
| render alone high, full-screen dirty | Tile flush cost | **Retune tile height** (today 8 px) only after measuring; prefer dirty-rect / smaller COMMIT regions; avoid full-screen CLEAR every tick when patching |
| fails only with arcs/text | Soft primitives | Simplify default widgets; pre-raster icons in asset packs; reduce `PROGRESS_ARC` use in hot paths |
| <30 ms retained re-render, slow only on push | SPI + interpret | OK for gate D if push path passes; local scroll should use retained re-render |

**Specific freeze decision:** if D fails on a typical notification list, **do not add heavier
primitives** until the tile renderer is faster. Prefer asset-atlas `ICON`/`IMAGE` over
procedural geometry for chrome.

## Pass criteria (hard)

| Gate | Metric | Pass |
|---|---|---|
| A | Sustained RX kB/s (watch) | ≥ 60 |
| B | DIAG RTT p95 | < 250 ms |
| D | parse_us + render_us (p95 and mean) | < 30 ms |

Log MTU, PHY, interval, and mbuf peak with every run.
