Working name: **Slate** (the watch firmware) and **SDP** (Slate Display Protocol). Rename freely — the names just make this document readable.


> **Changes in 0.2:** corrected the tile-buffer sizing (v0.1 had a factor-of-two error); added §7 the firmware freeze contract and extension opcodes; promoted **scripted sub-apps and the app repository** to a first-class subsystem (§6); added milestones M12–M13 and a phone-side project context file; corrected and re-verified all coding prompts.
>
> **Changes in 0.5:** added §4.5 defining every enumeration and flag field — `BEGIN_ELEM`, `TEXT_BOX` and `COMMIT` flag bits plus the eight single-value enums — closing a gap that affected 13 fields, not just the one reported; moved the shared-constants origin from M11 to M3, where it is first needed (§4.3); corrected "the last five" to four (§4.4); added the document conventions note establishing §4 as normative and this document as the single source of truth; renumbered §4.5/4.6 to §4.6/4.7 and updated the M3, M4 and M7 prompts to paste §4.5.
>
> **Changes in 0.4:** specified the `COLOR` and `STYLE` operand encodings, replacing the unimplementable "high bit signals palette mode" (§4.3); added `MULTI_TAP`, `EDGE_SWIPE`, `TOUCH_DOWN`/`TOUCH_UP` and a reserved range to the input vocabulary (§4.4); fixed `minHostVersion` to a semver string and stated why `minProtocolVersion` stays an integer (§6.8); separated power targets from gate hard-fail thresholds (§8, §10); documented channel 7 as debug-only (§4.2); clarified firmware ownership of session profiles (M7); updated the Kotlin and JS examples to the new operand types.
>
> **Changes in 0.3:** added §1.1 product positioning; expanded the v1 base opcode set with `POLYLINE`, `IMAGE`, `CLIP_RECT` and a shape style byte (§4.3); added §6.8 script API versioning and deprecation policy; added operational commitments to §6.1; made gates A–C explicitly blocking on SDP freeze (§10); added §14 standing caveats.

| | |
|---|---|
| **Project** | Slate — thin-client smartwatch OS for the PineTime |
| **Target hardware** | PINE64 PineTime (nRF52832, 64 KB RAM, 512 KB flash, 4 MB SPI NOR) |
| **Companion platform** | Android 11+ (minSdk 30), Kotlin |
| **Scope** | Watch firmware, phone bridge, display protocol, script platform, 18 agent-ready prompts |
| **Prompt target** | Claude Code or an equivalent agent with repo write access |
| **Date** | 27 July 2026 |
| **Status** | Internal working document — draft v0.5 |

> **What this document is**
>
> Part I sets out the architecture and explains why this shape and not another: a watch that only renders, a phone that runs the apps, and sub-apps that arrive as downloaded scripts rather than firmware. Part II gives the build order and the dependency spine. Part III is the executable half — 18 prompts, each with acceptance criteria, written to be pasted into a coding agent one at a time, in order. They are not independent: later prompts assume the interfaces earlier ones create.


## Document conventions

**Section 4 is normative.** The wire protocol — framing, opcodes, operand types, enumerations, flag bits, the session state machine — is defined in §4 and nowhere else. Every other part of this document, including all prompts, *references* §4 rather than restating it.

The prompts in Part III deliberately say `[paste §4.3]` instead of embedding a copy of the table. That is not laziness: a protocol table duplicated in eighteen places is a protocol table that will be wrong in some of them within a month. When you run a prompt, paste the current §4 text in at that moment. If you change §4, every prompt picks the change up automatically.

**This document is the single source of truth for the project.** It supersedes the earlier standalone architecture specification. If you find yourself copying §4 into a README, an issue, or a second document, reference it instead.

---

## Adopted decisions

| Decision | Adopted | Change it if… |
|---|---|---|
| Offline behaviour | **Resilient core** — local watch face, steps, alarms, retained screens | You want minimum firmware and accept a near-blank watch in dead zones |
| BLE compatibility | **Standard profiles + own protocol** — CTS, BAS, HRS, DIS, Nordic DFU; no InfiniTime emulation | You want Gadgetbridge as a fallback bridge |
| Kernel | **FreeRTOS + NimBLE, C++17** | Your team is stronger in Rust — swap to Embassy + TrouBLE; architecture unchanged |
| Sub-app model | **Scripted sub-apps (JS), downloaded from a repository** | — (this is now the core of the product) |
| Script engine | **`androidx.javascriptengine`** (V8, out-of-process) | You need lower call latency — use QuickJS in-process and accept a weaker sandbox |

---

---

# Part I — The design

## 1. The idea

The watch is a **rendering terminal with a resilient local core**. It draws what it's told, reports input, and independently keeps time, counts steps and fires alarms. Nothing else.

Everything that constitutes an "app" runs on the Android phone. The phone app is three things at once: a **bridge** (adapting notifications, media, navigation, health from other phone apps), a **host** (running sub-apps), and a **client** (pushing display lists over BLE and receiving input events).

Sub-apps are **scripts downloaded from a repository**, not code compiled into your app. That is the decisive property: a new watch feature requires no firmware OTA *and* no Play Store release. The user installs it from within your app, like a Bangle.js app loader.

**Three development speeds, deliberately separated:**

| Layer | Change requires | Target cadence |
|---|---|---|
| Watch firmware | OTA to every user | Yearly, ideally never |
| Bridge + adapters + script runtime | Play release | Quarterly |
| **Sub-apps** | **A download** | **Anytime, by anyone** |

The whole architecture exists to make the third row possible.

### 1.1 Positioning

**Slate is a programmable wrist display. It is not a notification bridge.**

This distinction has to be explicit in the product story, the README, the store listing and the repository front page — because the moment someone describes Slate as "notifications on your PineTime," it is competing head-on with Gadgetbridge, which is mature, free, well-supported and has years of device coverage. That is a comparison Slate loses.

What Gadgetbridge structurally cannot do is let you write a screen. Its device support is compiled in, per-device, by its maintainers. Slate's proposition is that **anyone can publish a wrist UI in an afternoon, in JavaScript, without touching firmware, without a Play release, and without owning a PineTime** (thanks to the desktop emulator).

Lead every piece of communication with that:

- **Primary use cases:** turn-by-turn navigation and workout HUD — the cases where the phone computes and the wrist displays, hands-free, phone stowed. On the OG PineTime the workout case is especially strong precisely *because* the watch has no GPS.
- **Secondary:** the long tail nobody will build for you — transit departures, CI status, tide tables, warehouse pick lists, a private hand in a tabletop game.
- **Deliberately not the headline:** notification mirroring. Slate should do it well because users expect it, but it is table stakes, not the pitch.

Practical consequence for the roadmap: M16 (navigation and camera) is not a "nice demo at the end." It is the first thing that demonstrates the actual product. Consider pulling a minimal navigation sub-app forward as soon as M13 lands.

---

## 2. System overview

```
┌──────────────────────────── ANDROID PHONE ─────────────────────────────┐
│  ┌── Repository client ──┐                                             │
│  │ browse · install ·    │  signed index over HTTPS                    │
│  │ update · remove       │◀────────────────  app repository            │
│  └──────────┬────────────┘                                             │
│             ▼                                                          │
│  ┌── Script Runtime (androidx.javascriptengine, isolated process) ──┐  │
│  │  nav.js   workout.js   timer.js   transit.js   <user's own>      │  │
│  │  each: render(ctx)→DisplayList · onInput(e) · onEvent(src,data)  │  │
│  └──────────┬───────────────────────────────────────────┬───────────┘  │
│             │  permission-gated API surface             │              │
│  ┌── Adapters ──────────────┐              ┌── Governor ──────────┐    │
│  │ NotificationListener     │              │ CPU · memory · radio │    │
│  │ MediaSession             │              │ timers · network     │    │
│  │ Health Connect           │              └──────────────────────┘    │
│  │ Intents / Gadgetbridge   │                                          │
│  │ CameraX, Location        │                                          │
│  └──────────┬───────────────┘                                          │
│  ┌──────────▼──────────────────────────────────────────────────────┐   │
│  │  Compositor (screen stack, priority, focus, per-app quotas)     │   │
│  ├─────────────────────────────────────────────────────────────────┤   │
│  │  Session Manager (negotiate, assets, credit-based flow control) │   │
│  ├─────────────────────────────────────────────────────────────────┤   │
│  │  Link Service (foreground service, CDM, GATT, reconnect)        │   │
│  └──────────────────────────────┬──────────────────────────────────┘   │
└─────────────────────────────────┼──────────────────────────────────────┘
                                  │  BLE / SDP
┌─────────────────────────────────▼──────────────────── PINETIME ────────┐
│  Link (NimBLE + SDP framing + channel demux)                           │
│  ┌──────────────────────┐  ┌────────────────────────────────────────┐  │
│  │  Remote screen stack │  │  Local screens (resilient core)        │  │
│  │  display-list interp │  │  watchface · notifications · settings  │  │
│  │  retained list (4KB) │  │  charging · disconnected               │  │
│  └──────────┬───────────┘  └───────────────┬────────────────────────┘  │
│             └───────────┬──────────────────┘                           │
│                ┌────────▼─────────┐                                    │
│                │  Tile renderer   │ ── SPI ──▶ ST7789 GRAM             │
│                └──────────────────┘                                    │
│  Core services: time · steps · alarms · settings · notif store · power │
│  Drivers: ST7789 · CST816S · BMA42x · HRS3300 · SPI-NOR · ADC · PWM    │
│  FreeRTOS · MCUBoot · LittleFS                                         │
└────────────────────────────────────────────────────────────────────────┘
```

**Ownership rule:** the watch owns *local* screens; the phone owns *remote* screens, pushed onto a stack. When the stack empties — back-navigation, timeout, or disconnection — the local watch face is revealed. The disconnected state falls out of the design rather than being bolted on.

---

## 3. Watch firmware

### 3.1 Tasks

| Task | Priority | Stack | Responsibility |
|---|---|---|---|
| `display` | high | 2.5 KB | Display-list interpreter, tile renderer, local screens |
| `link` | med-high | 2.0 KB | SDP framing, channel demux, session state machine |
| `sensors` | med | 1.0 KB | Accelerometer, step counting, battery, HR |
| `system` | low | 1.0 KB | Alarms, timers, power state machine, watchdog |
| `ble_host` | — | 3.0 KB | NimBLE host (created by NimBLE) |
| `ble_ll` | highest | 1.0 KB | NimBLE controller (created by NimBLE) |

### 3.2 RAM budget — 60,416 B allocatable

Apps living on the phone frees the entire 16 KB arena from the memory analysis. Reinvest it in rendering and protocol.

| Region | Budget | Notes |
|---|---|---|
| FreeRTOS heap (stacks + objects) | 14 KB | Stacks above plus queues, mutexes, timers |
| NimBLE mbuf pool + host state | 6 KB | Tune the pool; defaults are over-provisioned |
| **Tile render buffers** | **8 KB** | **Two 240×8px RGB565 tiles = 3,840 B each = 7,680 B** |
| Retained display list | 4 KB | Lets the watch redraw on wake/scroll without the phone |
| Asset working set (glyph/icon cache) | 6 KB | LRU over glyphs paged from external flash |
| Notification store | 4 KB | ~16 entries × 256 B |
| Protocol RX reassembly + TX buffers | 4 KB | |
| Local screen state | 3 KB | Watch face, step counts, alarm table |
| Settings + system state | 2 KB | |
| stdlib heap | 3 KB | |
| **Subtotal** | **54 KB** | |
| **Slack / reserve** | **~6 KB** | CI fails the build below 4 KB |

> **Corrected from v0.1.** The earlier draft claimed two 240×**16** tiles fit in 7,680 B. One such tile is 7,680 B; two are 15,360 B. The correct configuration for an 8 KB budget is **240×8 tiles**. Keep the tile height a compile-time constant so it can be retuned if the budget shifts.

**The hard floor this implies:** a full-screen redraw is 115,200 bytes over an 8 MHz SPI bus — the fastest the nRF52832 offers — which is **115 ms of pure transfer time, about 8 fps maximum**. Dirty-rectangle rendering is therefore not an optimisation, it is mandatory. A typical partial update touching 6 tiles takes ~23 ms. Full-screen redraws are acceptable as *transitions*; you cannot animate them.

The **retained display list** deserves emphasis. Without it, every backlight wake, scroll tick and partial redraw is a 60–200 ms round trip to the phone. With it, the watch re-renders locally at zero latency and zero radio cost. That 4 KB is what makes the architecture feel responsive rather than laggy.

### 3.3 Flash budget

**Internal, 512 KB:** MCUBoot 32 KB · firmware slot 320 KB · settings/keys/UICR 8 KB · spare ~150 KB.
The OTA secondary slot lives in **external** flash — MCUBoot supports this and it reclaims ~200 KB internally.

**External SPI NOR, 4 MB (LittleFS):** MCUBoot secondary slot 350 KB · asset packs 1.5 MB · notification/activity log ring 1 MB · spare ~1 MB.

### 3.4 The resilient core

Local screens, functional with no phone: **watch face** (time, date, battery, steps, connection status), **notification list** (retained store, browsable, staleness-marked), **settings**, **charging**, and the **disconnected** state.

Local capabilities: RTC timekeeping with drift correction on each sync; step counting from the BMA42x; alarms and timers that fire without the phone; settings persistence.

Explicitly **not** local: anything requiring interpretation of external data. No weather, no calendar, no music metadata beyond what was last pushed.

---

## 4. SDP — the wire protocol

### 4.1 GATT layout

Generate your own 128-bit base with `uuidgen`.

| | UUID | Properties |
|---|---|---|
| Service | `<base>-0000` | — |
| `RX` | `<base>-0001` | Write Without Response |
| `TX` | `<base>-0002` | Notify |
| `STATUS` | `<base>-0003` | Read, Notify |

Alongside: Current Time (client), Battery, Heart Rate, Device Information, and **Nordic legacy DFU as the recovery path**.

**Negotiate on every connection, in order:** ATT MTU 247 → Data Length Extension → 2M PHY → connection interval for the session state. All four are load-bearing; skipping DLE alone caps you near 54 kB/s.

### 4.2 Packet framing

ATT payload at MTU 247 is 244 bytes.

```
byte 0:  CHAN (3 bits) | FLAGS (5 bits)
         FLAGS: bit0 FIRST, bit1 LAST, bit2 ACK_REQ, bit3 URGENT, bit4 reserved
byte 1:  SEQ (0-255, wraps, per-channel)
[if FIRST and not LAST] bytes 2-3: total message length (u16 LE)
payload: remainder
```

| Channel | Name | Direction | Purpose |
|---|---|---|---|
| 0 | CONTROL | both | Negotiation, heartbeat, capability, time |
| 1 | DISPLAY | phone→watch | Display lists, framebuffer patches |
| 2 | INPUT | watch→phone | Touch, button, gesture, scroll |
| 3 | ASSET | phone→watch | Font/icon pack transfer, flow-controlled |
| 4 | SYSTEM | both | Notifications, alarms, settings, sensors |
| 5 | OTA | phone→watch | Firmware images |
| 6 | — | | Reserved |
| 7 | DIAG | both | Reserved; carries loopback and benchmark traffic in debug builds only |

Channel 7 is nominally reserved, but M5 and the measurement gates use it for loopback and throughput testing. That is deliberate: the benchmark harness needs a channel with no protocol semantics attached. **Debug builds only** — release firmware must reject all traffic on channels 6 and 7, and the negotiation capability flags must not advertise them.

### 4.3 Display list opcodes

Coordinates are `u8` (0–239). Two operand types are used throughout and must be implemented identically by the firmware interpreter, the Kotlin DSL and the JS builder.

**`COLOR` — variable length, 1 or 3 bytes**

```
byte 0: tag
  0x00        → literal RGB565 follows in bytes 1-2 (u16, little-endian)   [3 bytes]
  0x01 – 0x10 → palette entry (tag − 1), previously set by SET_PALETTE     [1 byte]
  0x11 – 0xFF → reserved; the parser must reject the list
```

RGB565 uses all sixteen bits — bit 15 is the red MSB — so there is no spare bit to flag palette mode inside the colour word. An explicit tag byte is the only encoding that does not corrupt the colour space. It also makes the common case *cheaper*: a palette reference is one byte rather than two, and most screens draw from a handful of colours.

**`STYLE` — `u8`**

```
bits 0-1  mode:   0 = fill, 1 = stroke, 2 = fill + stroke, 3 = reserved (reject)
bits 2-5  stroke width in pixels, 0-15; a value of 0 is treated as 1
bits 6-7  reserved, must be zero
```

Stroke width is ignored when mode is 0.

**Where these constants live.** Create a single shared constants header in **M3** — the firmware interpreter is the first thing that needs it. The M4 Kotlin DSL, the M12 JS builder and the M11 asset-pack tool all consume that same file; the M11 tool additionally *emits* the font and atlas IDs into it. One file, one origin, three consumers. Never transcribe these values into a second place.

| Op | Name | Operands |
|---|---|---|
| `0x01` | `CLEAR` | color:COLOR |
| `0x02` | `SET_PALETTE` | idx:u8, rgb565:u16 |
| `0x03` | `RECT` | x,y,w,h:u8, color:COLOR, style:STYLE |
| `0x04` | `RECT_ROUND` | x,y,w,h,r:u8, color:COLOR, style:STYLE |
| `0x05` | `LINE` | x0,y0,x1,y1:u8, color:COLOR, width:u8 |
| `0x06` | `CIRCLE` | cx,cy,r:u8, color:COLOR, style:STYLE |
| `0x07` | `ARC` | cx,cy,r:u8, a0,a1:u16, color:COLOR, width:u8 |
| `0x08` | `POLYLINE` | count:u8, color:COLOR, width:u8, count×(x:u8, y:u8) |
| `0x09` | `CLIP_RECT` | x,y,w,h:u8 |
| `0x0A` | `CLIP_CLEAR` | — |
| `0x10` | `TEXT` | font:u8, x,y:u8, color:COLOR, align:u8, len:u8, utf8[len] |
| `0x11` | `TEXT_BOX` | font:u8, x,y,w,h:u8, color:COLOR, align:u8, flags:u8, len:u8, utf8[] |
| `0x12` | `ICON` | atlas:u8, id:u16, x,y:u8, tint:COLOR |
| `0x13` | `IMAGE` | asset:u8, id:u16, x,y:u8 |
| `0x20` | `PROGRESS_BAR` | x,y,w,h:u8, pct:u8, fg:COLOR, bg:COLOR |
| `0x21` | `PROGRESS_ARC` | cx,cy,r:u8, pct:u8, fg:COLOR, bg:COLOR, width:u8 |
| `0x30` | `BEGIN_ELEM` | id:u16, x,y,w,h:u8, flags:u8 |
| `0x31` | `END_ELEM` | — |
| `0x40` | `SCROLL_REGION` | y,h:u8, content_h:u16 |
| `0x50` | `PATCH` | slot:u8, x,y,w,h:u8, format:u8, encoding:u8, len:u16, data[] |
| `0x51` | `PATCH_REF` | slot:u8, x,y:u8 |
| `0x60` | `HAPTIC` | pattern:u8 |
| `0x61` | `BACKLIGHT` | level:u8 |
| `0xE0`–`0xEF` | `EXT` | **see §7.2 — length-prefixed, forward-compatible** |
| `0xF0` | `COMMIT` | flags:u8 |
| `0xF1` | `RETAIN` | ttl_s:u16 |

A typical screen — clear, three text runs, an icon, commit — is **50–90 bytes**, depending on string lengths and on whether colours are palette references (1 byte) or literals (3 bytes). A representative case with 6-to-14-character runs and palette colours is 61 bytes. The connection interval, not bandwidth, is your frame limiter.

`BEGIN_ELEM`/`END_ELEM` make input latency-tolerant: the watch hit-tests locally and reports *which element* was touched.

`PATCH`/`PATCH_REF` are the escape hatch for genuine pixels. Keep individual patches under 4 KB.

**On the size of the v1 base set.** Every primitive you omit becomes either an ugly `PATCH` — expensive in bandwidth and battery — or a firmware OTA. Implement the whole table in v1 even if nothing uses half of it. Four ops earn their place specifically because emulating them is painful:

- **`POLYLINE`** — charts and sparklines are among the most-requested watch visuals (heart rate, pace, elevation). A 40-point sparkline is 39 `LINE` ops at 7–9 bytes each (depending on whether the colour is a palette reference or a literal) = 273–351 bytes, versus 84–86 bytes as a single `POLYLINE`. Three to four times smaller, on a payload you may push repeatedly.
- **`IMAGE`** — references a bitmap already stored in an asset pack, rather than re-sending pixels. Without it, any recurring full-width visual costs a `PATCH` every single time.
- **`CLIP_RECT` / `CLIP_CLEAR`** — genuinely hard to emulate from the phone, since the phone cannot know what is already on screen. Cheap in firmware, and required for clean scrolling content.
- **`style` byte** on `RECT`, `RECT_ROUND` and `CIRCLE` — stroke versus fill plus stroke width, for one byte. Emulating a stroked rectangle as four fills is four ops instead of one.

**The honest tension:** every opcode is parser surface area, and the display-list parser is the watch's main attack surface. More ops means more validation paths to get right and more to fuzz. The mitigation is structural — the ops share validation helpers (coordinate bounds, colour decode, length checks), so the marginal risk per op is small provided you build those helpers first and use them everywhere. Do not hand-roll bounds checks per opcode.

### 4.4 Input events (channel 2)

| Op | Name | Operands |
|---|---|---|
| `0x01` | `TAP` | elem_id:u16, x,y:u8 |
| `0x02` | `LONG_PRESS` | elem_id:u16 |
| `0x03` | `SWIPE` | dir:u8 |
| `0x04` | `BUTTON` | action:u8 |
| `0x05` | `SCROLL_POS` | region:u8, offset:u16 |
| `0x06` | `BACK` | — |
| `0x07` | `SESSION_END` | reason:u8 |
| `0x08` | `MULTI_TAP` | count:u8, elem_id:u16, x,y:u8 |
| `0x09` | `EDGE_SWIPE` | edge:u8, dir:u8, distance:u8 |
| `0x0A` | `TOUCH_DOWN` | elem_id:u16, x,y:u8 |
| `0x0B` | `TOUCH_UP` | elem_id:u16, x,y:u8, duration_ms:u16 |
| `0x0C`–`0x1F` | — | Reserved for future input events |

The last four exist because §7.1 says to over-provision the input vocabulary: adding an event later is an OTA, whereas defining one now costs a table row. `MULTI_TAP` subsumes double- and triple-tap (`count` = 2, 3). `EDGE_SWIPE` distinguishes a swipe starting at a screen edge from one starting in the middle, which is how most watches express back-navigation. `TOUCH_DOWN`/`TOUCH_UP` are emitted only when an element sets the `EMIT_TOUCH` flag (§4.5) — they are the escape hatch for a drag-like interaction, and carry the explicit warning that round-tripping them at 60–200 ms will feel bad.

Note that the CST816S reports a single touch point, so there is no pinch, rotate or two-finger event and none should be defined.

### 4.5 Enumerations and flag fields

Every `u8` enum and flag field in the two opcode tables, defined here so the firmware, the Kotlin DSL and the JS builder cannot invent their own. All reserved bits must be zero and all out-of-range values must cause the list to be rejected.

**`BEGIN_ELEM` flags**

```
bit 0  EMIT_TOUCH   emit TOUCH_DOWN / TOUCH_UP for this element
bit 1  NO_HIT       decorative grouping only; excluded from hit-testing entirely
bit 2  HAPTIC       fire the default haptic on tap, watch-local, no round trip
bit 3  FOCUSABLE    participates in focus traversal and selection
bit 4  DISABLED     drawn (the sub-app may dim it) but reports no input
bits 5-7 reserved, must be zero
```

`NO_HIT` and `DISABLED` are not the same thing: `NO_HIT` means the element is never interactive, `DISABLED` means an interactive element is currently unavailable. Keep both — collapsing them loses the ability to render a greyed-out button that still occupies the hit-test table for layout purposes.

**`TEXT_BOX` flags**

```
bit 0  WRAP           wrap at word boundaries within w
bit 1  ELLIPSIS_END   truncate with an ellipsis rather than clipping
bit 2  VCENTER        centre vertically within h
bits 3-7 reserved
```

**`COMMIT` flags**

```
bit 0  FADE      cross-fade from the previous frame instead of a hard swap
bit 1  NO_CLEAR  composite over the retained frame; do not implicitly clear
bits 2-7 reserved
```

**Single-value enumerations**

| Field | Values |
|---|---|
| `align` (TEXT, TEXT_BOX) | 0 LEFT · 1 CENTER · 2 RIGHT |
| `format` (PATCH) | 0 RGB565 · 1 RGB332 · 2 PAL4 · 3 MONO1 |
| `encoding` (PATCH) | 0 RAW · 1 RLE |
| `pattern` (HAPTIC) | 0 TICK · 1 SHORT · 2 DOUBLE · 3 LONG · 4 ERROR |
| `dir` (SWIPE, EDGE_SWIPE) | 0 UP · 1 DOWN · 2 LEFT · 3 RIGHT |
| `edge` (EDGE_SWIPE) | 0 TOP · 1 BOTTOM · 2 LEFT · 3 RIGHT |
| `action` (BUTTON) | 0 PRESS · 1 LONG_PRESS · 2 DOUBLE_PRESS |
| `reason` (SESSION_END) | 0 USER_BACK · 1 TIMEOUT · 2 LINK_LOST · 3 PHONE_REQUEST · 4 ERROR |

These live in the same shared constants header as `COLOR` and `STYLE` (§4.3).

The watch owns scroll position, selection focus and back-navigation *within* a pushed screen, with zero latency and instant haptic feedback. Only discrete semantic events cross the link. Never round-trip a drag.

### 4.6 Session state machine

```
DISCONNECTED ──advertise──▶ CONNECTED ──negotiate──▶ READY
                                 ▲                     │
                                 │                push screen
                          link loss / timeout          ▼
                                 └──────────────  ACTIVE ⇄ IDLE
```

Negotiation: the watch reports firmware and protocol version, screen geometry, a **32-byte supported-opcode bitmap**, free display-list bytes, supported session profiles, and asset-pack hashes. The phone replies with session config and pushes missing asset packs over channel 3.

Heartbeat every 2 s. Two missed → staleness overlay on retained remote screens. Five missed → pop the remote stack, reveal the watch face.

### 4.7 Flow control

DISPLAY is credit-based: the watch advertises free buffer bytes at negotiation and after each `COMMIT`. ASSET is strictly windowed and yields to DISPLAY and INPUT — a font transfer must never make the UI stutter. Remember the SPI bus is shared with the display, so asset writes contend with rendering.

---

## 5. Phone application

### 5.1 Components

| Component | Responsibility |
|---|---|
| **Link Service** | Foreground service (`connectedDevice` type), GATT, reconnection, CDM presence |
| **Session Manager** | Negotiation, asset sync, heartbeat, credit accounting |
| **Compositor** | Screen stack, priority arbitration, focus, per-app push quotas |
| **Script Runtime** | Sandboxed JS execution, API bindings, lifecycle (§6) |
| **Governor** | CPU, memory, radio, timer and network budgets per sub-app (§6.5) |
| **Repository Client** | Browse, install, update, remove sub-apps (§6.6) |
| **Adapters** | The bridge surface to other phone apps |
| **Display List DSL** | Kotlin *and* JS builders producing byte-identical output |
| **Emulator** | Desktop renderer — develop sub-apps with no watch and no phone |

### 5.2 Companion Device Manager

Use `CompanionDeviceManager` with the **`watch` device profile** — it assigns the profile role and grants its permission bundle, and Google positions it as the sanctioned path for wearables. Pair with `CompanionDeviceService`, `REQUEST_COMPANION_RUN_IN_BACKGROUND` and `startObservingDevicePresence()`.

Since Android 15, background BLE requires `foregroundServiceType="connectedDevice"`. Budget for a persistent notification and expect to prompt Samsung/Xiaomi/Huawei users for battery-optimisation exemption.

### 5.3 Adapters

| Adapter | Android API | Gives you |
|---|---|---|
| Notifications | `NotificationListenerService` | All notifications + actions |
| Media | `MediaSession` / `MediaController` | Metadata and transport for *any* player |
| Fitness | Health Connect | Read workouts; write watch HR and steps |
| Navigation | OsmAnd intents + a generic nav interface | Turn-by-turn maneuvers |
| Automation | Broadcast Intents; **Gadgetbridge Intent API** as client | Tasker/Home Assistant drive your watch for free |
| Camera | CameraX, downscaled analysis target | Preview frames, RGB332-converted on the phone |

---

## 6. Scripted sub-apps and the repository

This is the core of the product. A sub-app is a signed bundle of JavaScript and metadata, downloaded from a repository inside your app, executed in a sandbox, producing display lists.

### 6.1 Legal footing

Google Play's Device and Network Abuse policy prohibits downloading **executable code** (dex, JAR, `.so`) from outside Play — but explicitly **does not** apply to code running in a VM or interpreter with indirect access to Android APIs. Interpreted languages loaded at runtime are permitted.

The binding condition, clarified by Google in October 2021: interpreted code loaded at runtime **must not enable violations of Play policy**. Practical consequences you must design for:

- **Curate the official repository.** You are responsible for what your app can be made to do.
- **Publish a content policy** and a takedown process.
- **User-added third-party repositories** need an explicit trust prompt, and should be restricted to a reduced permission set (no network, no health, no location) unless the user opts in per-app.
- Permissions granted to a script can never exceed those held by your app.

**These are operational commitments, not one-off engineering tasks.** Curating a repository means reviewing every submission and every update, responding to reports, and maintaining a takedown process — indefinitely, and mostly unpaid. A solo developer should be honest about whether that is a commitment they want before opening submissions.

A sensible de-risking sequence:

1. **Launch closed.** The official repository contains only sub-apps you wrote. All the machinery — signing, install, permissions, versioning — is exercised, with no moderation load.
2. **Open by invitation.** Add contributors you know. Review is a pull request against the repository index in a public git repo, so review history is transparent and the process scales through normal code-review habits.
3. **Open submissions** only once you have a written content policy, a takedown path, and either co-maintainers or automated checks (manifest validation, permission diffing between versions, static analysis for obvious abuse patterns).

Hosting the index in a public git repository is worth doing from step one: pull requests give you review, history and attribution for free, and it is exactly how the Bangle.js app loader operates.

### 6.2 Package format

```
com.example.transit.slate      (a zip)
├── manifest.json
├── main.js
├── icon.png                   (phone-side launcher, 96×96)
├── assets/                    (optional — icon atlas fragments pushed to the watch)
│   └── icons.pack
└── README.md
```

```json
{
  "id": "com.example.transit",
  "name": "Transit Departures",
  "version": "1.2.0",
  "author": "Jane Dev",
  "license": "MIT",
  "description": "Next departures for your saved stops.",
  "minProtocolVersion": 2,
  "minHostVersion": "1.3",
  "entry": "main.js",
  "priority": "normal",
  "refresh": { "policy": "periodic", "intervalMs": 60000 },
  "permissions": ["http", "location", "storage"],
  "http": { "allowedHosts": ["api.transit.example"] },
  "requiredAssets": [{ "atlas": "icons", "sha256": "..." }]
}
```

`minProtocolVersion` is the mechanism that lets a sub-app depend on a newer firmware feature. The compositor refuses to focus a sub-app whose requirement exceeds the connected watch, and shows an "update your watch" screen instead.

### 6.3 Script API

```js
export function onFocus(ctx)            { }   // gained the screen
export function render(ctx)             { }   // returns a display list — must be pure and fast
export function onInput(event)          { }   // TAP, LONG_PRESS, SWIPE, BUTTON, BACK
export function onEvent(source, data)   { }   // adapter push: notification, media, location…
export function onBlur()                { }
```

Provided globals, each permission-gated:

| Binding | Permission | Purpose |
|---|---|---|
| `slate.ui` | — | Display-list builder, mirrors the Kotlin DSL |
| `slate.invalidate()` | — | Request a re-render |
| `slate.store` | `storage` | Per-app key-value persistence, quota'd |
| `slate.http` | `http` | Fetch, restricted to `allowedHosts` |
| `slate.notifications` | `notifications` | Subscribe to notification events |
| `slate.media` | `media` | Media state and transport control |
| `slate.location` | `location` | Coarse or fine location |
| `slate.health` | `health.read` | Health Connect reads |
| `slate.timer` | — | Budget-limited timers, minimum 1000 ms |
| `slate.haptic` | — | Request a haptic pattern |
| `slate.log` | — | Diagnostics, visible in the app's dev console |

```js
export function render(ctx) {
  return slate.ui.displayList(b => {
    b.clear(slate.PAL[0]);
    b.icon("nav", ctx.state.maneuverIcon, 96, 30, slate.PAL[1]);
    b.text("LARGE", 120, 110, "CENTER", `${ctx.state.distance} m`);
    b.textBox("SMALL", 10, 150, 220, 60, "CENTER", ctx.state.street);
    b.element(1, 0, 200, 240, 40, () => {
      b.rectRound(0, 200, 240, 40, 8, slate.rgb(0x4208), slate.FILL);
      b.text("SMALL", 120, 212, "CENTER", "End");
    });
    b.retain(300);
    b.commit();
  });
}
```

### 6.4 Sandbox

**Recommended: `androidx.javascriptengine`.** It runs V8 in an **isolated process exclusive to your app**, supports multiple isolates with low overhead, needs no WebView instance, and can run inside a Service. For code downloaded from a repository, process isolation is the right default — a sandbox escape costs an isolated process, not your app.

The trade-off is a string-in/string-out IPC boundary (with `provideNamedData` for byte arrays), so per-call latency is higher than in-process. Since `render()` is called at most a few times per second, this is acceptable. **Measure it** — if IPC latency turns out to dominate, QuickJS in-process is the fallback, with a strictly whitelisted binding surface and no reflection.

Design the binding layer so it could move across a process boundary either way. That keeps the choice reversible.

### 6.5 Resource governance

A misbehaving script cannot hurt the watch — the watch only ever receives data — but it can drain the *phone* battery and saturate the radio. The Governor enforces:

| Resource | Limit |
|---|---|
| `render()` execution | 50 ms, then kill and blank the app |
| `onEvent()` / `onInput()` | 200 ms |
| Heap per isolate | 4 MB |
| Display pushes | 10/s foreground, 1/min ambient, credit-limited by the compositor |
| Timers | minimum 1000 ms interval; sub-second denied |
| Network | `allowedHosts` only; request quota per hour |
| Storage | 256 KB per app |

Repeated violations disable the sub-app and surface a diagnostic to the user. Log everything to an in-app dev console — sub-app authors need it.

### 6.6 The repository

Model it on the Bangle.js app loader: a **static, signed JSON index** hosted anywhere (GitHub Pages is sufficient), with no server-side component.

```json
{
  "schema": 1,
  "updated": "2026-07-27T00:00:00Z",
  "apps": [
    {
      "id": "com.example.transit",
      "version": "1.2.0",
      "name": "Transit Departures",
      "description": "…",
      "author": "Jane Dev",
      "minProtocolVersion": 2,
      "minHostVersion": "1.3",
      "permissions": ["http", "location", "storage"],
      "size": 14203,
      "sha256": "…",
      "url": "https://apps.example.org/com.example.transit-1.2.0.slate",
      "screenshots": ["…"]
    }
  ]
}
```

- **Signing:** sign the *index* with **Ed25519**; the index carries a SHA-256 per package. One trusted key, and package integrity follows. (Firmware signing stays ECDSA-P256 because MCUBoot requires it — different trust domain, don't conflate them.)
- **Multiple repositories:** users may add URLs. Show provenance clearly, apply the reduced permission set from §6.1, and never let a third-party repo shadow an official app ID.
- **Updates:** check the index on a schedule; respect metered-connection settings; never auto-install a version that raises the permission set without asking.
- **Offline:** cache installed packages locally. Installing needs the network; *running* an installed sub-app must not.

### 6.7 Developing a sub-app

The emulator (M4) plus the script runtime means **a sub-app author needs neither a watch nor a phone**: write JS, run it against the desktop emulator, see the rendered 240×240 output, simulate taps. Ship this as a documented workflow from day one — it is the single biggest determinant of whether anyone else writes sub-apps for you.

### 6.8 The script API is a public contract

The moment a third party installs a sub-app someone else wrote, the binding surface in §6.3 stops being an implementation detail and becomes an API you owe compatibility to. Version it deliberately from the first release, not the first time it hurts.

**Two independent version axes**, both already present in the manifest:

| Field | Governs | Owner |
|---|---|---|
| `minProtocolVersion` | SDP — what the *watch firmware* can render | Firmware |
| `minHostVersion` | The script API — what the *phone app* provides | Phone app |

A sub-app needing `POLYLINE` raises `minProtocolVersion`. A sub-app needing a new `slate.health` method raises `minHostVersion`. Conflating them means a phone-app feature appears to require a firmware update, which is exactly the coupling this architecture exists to avoid.

**Versioning rules:**

- The script API uses **semantic versioning**. `minHostVersion` is a **string** of the form `"MAJOR.MINOR"` — the major version the sub-app targets plus the minimum minor it needs. Compare it numerically, field by field, never lexically: `"1.10"` is newer than `"1.9"`.
- `minProtocolVersion` is by contrast a **plain integer**. SDP has a single monotonic version counter with no minor component, because a protocol change is either compatible (a new extension opcode, signalled through the capability bitmap) or it is a version bump. Keeping the two fields visibly different types is deliberate — it makes conflating them harder.
- **Additive changes** — new bindings, new optional parameters — bump minor. Existing sub-apps keep working untouched.
- **Breaking changes** bump major and require a migration window. Do not take them lightly; every one strands sub-apps whose authors have moved on.
- **Deprecation window: two minor versions minimum.** Mark deprecated, warn loudly in the developer console and in the repository review process, then remove.
- The repository index carries `minHostVersion` per app, so the client hides or greys out sub-apps the installed host cannot run — with the reason shown, never a silent failure.

**Design the surface small.** Every binding is a promise. It is far easier to add `slate.calendar` in v1.3 than to remove it in v2.0. When in doubt, leave it out and let a sub-app author ask for it — that request is also your evidence that someone wants it.

**Keep an API changelog in the repository from day one**, and treat the §6.3 table as its canonical source. Sub-app authors will read it more often than any other document you write.

---

## 7. The firmware freeze contract

The goal is to write the firmware once and change it rarely. That holds only if you deliberately design the following to be extensible **over the protocol** rather than compiled in. Get these right before the first public release; changing them later means an OTA to every user.

### 7.1 The freeze checklist

| Item | Over-provision now | Why |
|---|---|---|
| **Opcode space** | Implement the full §4.3 set even if unused; reserve `0xE0`–`0xEF` for extensions | Adding a primitive later is an OTA |
| **Input vocabulary** | Already done in §4.4: `MULTI_TAP`, `EDGE_SWIPE`, `TOUCH_DOWN`/`TOUCH_UP` are defined but unused, and `0x0C`–`0x1F` is reserved | Events are nearly free; new ones are an OTA |
| **Session profiles** | Make power/refresh policy **negotiable**, not hardcoded. The phone requests a named profile | A new app category otherwise needs new firmware |
| **Channel IDs** | Keep 6–7 reserved | Cheap now, impossible later |
| **Asset pack format** | Version field, forward-compatible index, unknown record types skipped by length | Lets you add font or icon features without firmware |
| **Capability bitmap** | 32 bytes covering the whole opcode space, plus a feature-flag word | The mechanism for graceful degradation |
| **Fonts/icons** | Always runtime assets, never compiled in | Already designed this way — keep it that way |

### 7.2 Extension opcodes

Opcodes `0xE0`–`0xEF` are **length-prefixed** so unknown ones can be skipped safely:

```
byte 0:      opcode (0xE0-0xEF)
bytes 1-2:   operand length (u16 LE)
bytes 3..:   operands
```

Firmware that doesn't recognise the opcode advances by `3 + length` and continues rendering. Firmware that does, handles it. Combined with the capability bitmap — the phone knows which extension opcodes the connected watch supports — this gives you a genuine forward-compatibility path.

**The rule this establishes:** the base opcode set (`0x01`–`0x61`, `0xF0`–`0xF1`) is frozen and must never change meaning. All future primitives arrive in the extension range. Two bytes of overhead per extension op buys you years of not shipping firmware.

### 7.3 Versioning discipline

- Each sub-app declares `minProtocolVersion`; the compositor blocks and explains rather than failing at render time.
- **Your phone app must keep working against the oldest firmware you ever shipped.** This is an ongoing constraint, not a formality. Keep old firmware images and test against them in CI.
- Bump the protocol version only on incompatible change. New extension opcodes are a *capability bitmap* change, not a version bump.

### 7.4 What still forces a firmware update

Be honest with yourself about these:

- A drawing primitive common enough that `PATCH` is too expensive
- A new input gesture
- Resilient-core behaviour: watch faces, step algorithm, alarm semantics
- BLE stack updates, security fixes, display-list parser bugs

Expect roughly one firmware release a year. Everything else should be a phone update or a sub-app download.

---

## 8. Power policy

| State | Backlight | Conn. interval | Est. current | Duty |
|---|---|---|---|---|
| **Ambient** | off | 500 ms – 1 s | <200 µA avg | ~99% |
| **Glance** | low/mid | unchanged | ~10 mA | seconds |
| **Active session** | mid | 30–50 ms | ~20–25 mA | minutes |
| **Streaming** (patch tier) | mid/high | 15 ms | ~25–30 mA | hard time-boxed |

Reference: backlight high/mid/low = 12.27 / 5.51 / 1.83 mA, LCD active 5.61 mA, achievable sleep baseline ~66 µA.

The figures above are **targets**. Gate C in §10 sets the hard-fail thresholds 25% looser (active >30 mA, ambient >250 µA) as deliberate engineering margin: missing a target means tuning, exceeding a hard fail means the power model itself is wrong and the session design has to change.

Against 180 mAh: a **30-minute navigation session costs ~11 mAh, about 6% of the battery.** Design and market around that number.

Session profiles are **negotiated** (§7.1), so a sub-app declares its refresh policy in its manifest and the Session Manager maps that onto a profile the watch already understands.

---

## 9. Security model

The strongest property: **the display-list stream is data, never code.** The watch executes nothing it receives. Compare any loadable-app design, where the phone gets a direct code-execution channel.

| Surface | Control |
|---|---|
| BLE link | LE Secure Connections pairing + bonding; control channels reject unbonded peers |
| Firmware | MCUBoot + ECDSA-P256, public key in internal flash, APPROTECT enabled |
| **Display-list parser** | **The main watch-side attack surface.** Validate every coordinate, length, font/atlas/element ID *before* use. Cap ops per list, parse time, string lengths. Reject and resynchronise — never fault |
| Asset pack index | Untrusted structure — bound it identically |
| **Sub-app scripts** | **The main phone-side attack surface.** Isolated process, permission-gated bindings, Governor budgets, signed repository index, curated official repo |
| Repository | Ed25519-signed index with per-package SHA-256; third-party repos get reduced permissions and a trust prompt |
| Android surface | CDM association required; no exported component that lets an arbitrary app push screens |

**Worst case from a hostile sub-app:** draw something misleading, drain the phone battery, or exfiltrate data it was granted permission to read. Bounded, and none of it is code execution on the watch.

Fuzz both parsers — the display-list interpreter and the asset-pack index. Both are trivially fuzzable off-device.

---

## 10. Measurement gates

**Gates A, B and C are blocking on the SDP freeze.** Not advisory, not "nice to have before v1" — blocking. Three numbers decide whether the product is what this document assumes it is:

- **A (throughput)** decides whether the patch tier is real. If sustained throughput lands well under 60 kB/s, camera preview and any pixel-heavy visual are off the table, and `IMAGE` plus a richer opcode set become more important rather than less.
- **B (tap RTT)** decides how interaction has to be designed. If p95 exceeds ~250 ms, more state must move to the watch — which is a *firmware* change, so you need to know before freezing.
- **C (ambient current)** decides whether this feels like a watch or like a gadget. If ambient draw is materially above 200 µA, week-long battery life is gone, and that reshapes the session model and possibly the always-on assumptions in §8.

Everything else in the design can wait on those three numbers. Measuring them costs a devkit, an SWD probe, a Power Profiler Kit II and a couple of days. Freezing a protocol around unmeasured assumptions costs an OTA to every user.

Gate D (render time) is a close fourth: it is a firmware design input, and the 115 ms full-redraw floor from §3.2 already tells you partial rendering is mandatory. Gate F is host-side and can wait for Phase 4.

| Gate | Measure | Threshold | If it fails |
|---|---|---|---|
| **A. Throughput** | Sustained kB/s to 3 handsets, DLE + 2M PHY + MTU 247 | ≥60 kB/s all | Shrink patch budgets; drop streaming tier |
| **B. Latency** | Tap→response RTT distribution | p95 <250 ms | Move more interaction state to the watch |
| **C. Power** | Current per state, Nordic PPK II | Target: active <25 mA, ambient <200 µA. **Hard fail: active >30 mA or ambient >250 µA** | Revisit backlight and interval policy |
| **D. Render time** | Parse + render a typical 80-byte list | <30 ms | Retune tile height; optimise renderer |
| **E. Reconnect** | Back-in-range → READY | <5 s | Revisit advertising and CDM config |
| **F. Script latency** | `render()` round trip through the JS sandbox | <20 ms | Move from `javascriptengine` to QuickJS in-process |
| **G. Fuzz** | Both parsers, 24 h libFuzzer | Zero crashes | Fix before any release |

Gates A–D run at the end of Phase 1. Gate F runs at the end of Phase 4.

---

## 11. Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| Throughput <60 kB/s on target handsets | Patch tier and camera unviable | Gate A before protocol freeze; display-list tier unaffected |
| Session power >25 mA | Feature feels battery-hostile | Gate C; tighter backlight defaults, shorter auto-timeout |
| Script IPC latency dominates | Sluggish UI | Gate F; QuickJS in-process fallback, binding layer designed to move |
| Android OEM battery killers | Link dies silently | CDM `watch` profile + foreground service + in-app diagnostics |
| **Play policy challenge on downloaded scripts** | **App removal** | Interpreted code is explicitly permitted; curate the official repo, publish a content policy, restrict third-party repo permissions |
| Nobody writes sub-apps | No ecosystem | The no-hardware dev workflow (M4 + M12) is the single biggest lever — document and promote it |
| Display-list or asset parser bug | Remote crash | Fuzz from M3 and M11; gate G before release |
| Bad OTA on a sealed watch | Unrecoverable brick | Nordic DFU recovery, confirm-on-BLE-connect, battery precondition |
| iOS demand | Architecture is Android-shaped | Be explicit: v1 is Android-only |
| Font metric divergence | UI wrong on device | One asset pack from one tool (M11); golden-file tests across firmware, Kotlin DSL and JS builder |

---

## 12. What this architecture gives up

- **No offline sub-apps.** Beyond the resilient core, everything needs the phone. A runner leaving their phone at home gets a watch, a step counter and alarms.
- **iOS is a poor fit.** ANCS/AMS invert the GATT roles and background BLE is restricted.
- **Session latency is visible.** 60–200 ms round trips mean discrete taps, never drags. Watch-local scroll and focus mitigate this; it will never feel native.
- **~6% of battery per 30-minute session.** Heavy users will notice.
- **You compete with Gadgetbridge** on notifications, and it is mature. Lead with navigation and workout — the cases where a programmable wrist UI does something Gadgetbridge structurally cannot. See §1.1; this needs to be explicit in the product story, not just understood internally.
- **You now own an API.** The script binding surface is a public contract. Breaking it breaks other people's sub-apps. That is the price of the ecosystem.

The compensating advantage: **a sub-app costs zero bytes on the watch, ships without a Play release, and can be written by anyone with a text editor and the desktop emulator.** For a small team, that trade is almost certainly correct.

---

## 13. Standing caveats

Five things that stay true throughout the project. None is a blocker; all are easy to lose sight of mid-build. Re-read this list at each phase boundary.

**1. Prove gates A, B and C before freezing SDP.**
Throughput, tap RTT and ambient current decide whether navigation and camera are viable and whether this feels like a watch. Everything else can wait on those numbers. Freezing a protocol around unmeasured assumptions costs an OTA to every user. → §10

**2. Ship a rich enough base opcode set in v1.**
Extension opcodes help later, but every common primitive you omit becomes either an ugly `PATCH` — expensive in bandwidth and battery — or a firmware update. Err toward implementing unused ops early. The parser surface-area cost is real but manageable if all ops share the same validation helpers. → §4.3, §7.1

**3. The script API is a public contract.**
That is the price of the repository model. Version it deliberately from day one, on two independent axes: `minProtocolVersion` for firmware capability, `minHostVersion` for the script API. Keep the surface small — adding a binding is easy, removing one strands other people's work. → §6.8

**4. Play policy is workable but operational.**
A curated official repository, a written content policy, and reduced permissions for third-party repos are load-bearing, not polish. Curation is ongoing unpaid work; launch closed and open up in stages rather than committing to moderation you have not planned for. → §6.1

**5. Keep the Gadgetbridge differentiation explicit.**
Slate is a programmable wrist UI for navigation and workout. It is not another notification bridge — that comparison is one Slate loses. Notifications should work well; they should never be the pitch. → §1.1

---

---

# Part II — Build order

## 14. Ordering logic
Three constraints determine almost the whole order.

- **Measure before you freeze.** Gates A, B and C sit deliberately between M6 and M7. Throughput, tap latency and ambient current decide whether the patch tier is real, how much interaction state must live on the watch, and whether week-long battery life survives. All three are firmware-shaped questions, so they must be answered before the protocol and the power model harden.
- **The emulator is early, not late.** M4 has no hardware dependency beyond byte-compatibility with M3. It unblocks every piece of UI work that follows, and it is a hard prerequisite for the script platform in M12 — a sub-app author who needs a watch and a phone to see their work is a sub-app author you will not get.
- **Recovery before distribution.** M15 carries the Nordic DFU fallback. The sealed PineTime has no accessible SWD, so no firmware should reach a device you cannot get back before that path exists. This is why M15 is production-phase work that nonetheless cannot slip past the first external release.

Everything else is scheduling preference. The two Android-side workstreams (M8/M9 and M12/M13) have no firmware dependency once M6 lands, so a second developer can run them in parallel with M10/M11 on the watch.

### 14.1 The dependency spine

```
Phase 0 — Foundations
 │ M0  toolchain, linker, UICR, RTT        ◀── gates absolutely everything
 │ M1  display + tile renderer         ◀── M0
 │ M2  touch, button, gestures         ◀── M0
 │
 ├─▶ Phase 1 — Vertical slice
 │    │ M5  BLE, GATT, SDP framing     ◀── M0      (parallel with M3/M4)
 │    │ M3  display-list interpreter   ◀── M1
 │    │ M4  Kotlin DSL + emulator      ◀── M3      (no hardware needed)
 │    │ M6  VERTICAL SLICE             ◀── M3 + M4 + M5
 │    │      │
 │    │      ▼
 │    │ GATES A–D                      ◀── BLOCKING on the SDP freeze
 │    │
 ├─▶ Phase 2 — Protocol hardening                    ─┐
 │    │ M7  input path + session       ◀── M2, M6     │ watch + phone
 │    │ M8  compositor + app host      ◀── M6         │ can run in
 │    │ M9  adapters + notifications   ◀── M7, M8     │ parallel from
 │    │                                               │ here on
 ├─▶ Phase 3 — Resilience and assets                  │
 │    │ M10 resilient core             ◀── M1, M2, M7 │
 │    │ M11 asset packs + glyph cache  ◀── M1, M3     │
 │    │                                              ─┘
 ├─▶ Phase 4 — The script platform
 │    │ M12 script runtime + sandbox   ◀── M4, M8
 │    │ M13 repository + installer     ◀── M12
 │    │
 └─▶ Phase 5 — Production
      │ M14 power optimisation         ◀── M7, M10
      │ M15 OTA + recovery             ◀── M0, M5    ◀── before ANY external release
      │ M16 nav + camera sub-apps      ◀── M9, M11, M13
```

### 14.2 Phase schedule

| Phase | Prompts | Outcome | Indicative effort |
|---|---|---|---|
| Phase 0 — Foundations | M0 – M2 | Board is alive: display, input and a trustworthy build | 4–6 weeks |
| Phase 1 — Vertical slice | M3 – M6 + gates | A phone pushes a screen, and the three blocking numbers are measured | 6–9 weeks |
| Phase 2 — Protocol hardening | M7 – M9 | Real interaction, real notifications, a session that survives link loss | 6–8 weeks |
| Phase 3 — Resilience and assets | M10 – M11 | Works with no phone; fonts and icons ship as data | 5–7 weeks |
| Phase 4 — The script platform | M12 – M13 | Sub-apps download from a repository and run sandboxed | 7–10 weeks |
| Phase 5 — Production | M14 – M16 | Power targets met, updates recoverable, flagship sub-apps shipped | 10–14 weeks |

Roughly **38–54 engineer-weeks** in total, or nine to thirteen months for one competent developer working steadily. That is build time only: it excludes learning curve, hardware procurement, and the rework that measurement gates exist to trigger. The earlier estimate of one to three person-years for the whole product still stands once iteration is included.

### 14.3 Prompt anatomy

Each entry below gives its phase, goal, the files it touches, what it depends on and a rough size, then the prompt itself, then acceptance criteria. Sizes are engineer-days for a competent human reviewing agent output, not agent wall-clock.

The prompts are **not independent**. Later ones assume the interfaces earlier ones create, and several paste sections of Part I directly into the prompt body. Run them in order, one per session.

### 14.4 How to prompt for this project

- **Demand concrete register writes and linker scripts.** Reject "you'll need to configure the SPI peripheral" — ask for the actual writes.
- **Embed the datasheet facts in every prompt.** Models reliably hallucinate pin assignments. The prompts below do this deliberately.
- **One subsystem per session.** Embedded bugs compound.
- **Ask for the test first** on anything parsing untrusted input.
- **Read every line of these four:** flash layout, OTA path, display-list parser bounds checks, pairing/bonding.

### 14.5 Firmware project context — `CLAUDE.md` at the firmware repo root

```markdown
# Project: Slate firmware — thin-client smartwatch OS for PineTime

## Hardware (authoritative — do not guess)
- MCU: Nordic nRF52832, Cortex-M4F @64MHz, 512KB internal flash, 64KB SRAM, ARMv7-M MPU
- Display: ST7789, 240x240 RGB565, SPI **mode 3**, max 8MHz. CS=P0.25, SCK=P0.02,
  MOSI=P0.03, DC/RS=P0.18, RST=P0.26. Backlight active-low on LOW/MID/HIGH pins.
- External flash: XT25F32B 4MB SPI NOR, CS=P0.05, **shares the SPI bus with the display**.
  Not memory-mapped — no XIP. Assert only one CS at a time.
- Touch: Hynitron CST816S, I2C 0x15, SDA=P0.06, SCL=P0.07, RST=P0.10, IRQ=P0.28.
  Sleeps when idle and stops responding on the bus — this is NORMAL. All TWI ops need timeouts.
  Gesture IDs: 0x01 slide-down, 0x02 slide-up, 0x03 slide-left, 0x04 slide-right,
  0x05 single-tap, 0x0B double-tap, 0x0C long-press.
- Accel: BMA421 (pre-Jul-2021) or BMA425 (after), I2C 0x18, IRQ=P0.08. Detect at runtime.
- HR: HRS3300, I2C. Enabled at power-on — write 0x00 to PDRIVER (0x0C) to sleep it.
- Button: drive P0.15 high to read P0.13; costs 34uA if left high — strobe it.
- Battery: ADC AIN7 (P0.31). mV = adc * 2000 / 1241. Charge indicator P0.12 (low = charging).
- NFC unavailable: P0.10 is the touch reset, so UICR NFCPINS must select GPIO.

## Stack
FreeRTOS + NimBLE + LittleFS + MCUBoot. C++17. No LVGL — custom tile renderer.

## Hard constraints
- RAM: total static + heap under 54KB, >=6KB slack. CI enforces.
- NO full framebuffer. Render two 240x8 RGB565 tiles (3,840B each, 7,680B total) and
  DMA them out. A full-screen redraw is 115,200 bytes = ~115ms at 8MHz, so dirty-rect
  rendering is mandatory, not an optimisation.
- The watch NEVER executes anything received over BLE. Display lists are data only.
- Every BLE-facing parser validates all lengths, indices and coordinates before use,
  and must reject-and-resync rather than fault.
- Base opcodes 0x01-0x61 and 0xF0-0xF1 are FROZEN. New primitives go in the
  length-prefixed extension range 0xE0-0xEF so old firmware can skip them.

## Architecture
Thin client. The phone pushes display lists; the watch renders and returns element-level
input events. A resilient local core (watch face, steps, alarms, retained screens) works
with no phone. Protocol in slate-implementation-roadmap.md (§4).

## Conventions
- Drivers are C++ classes, no dynamic allocation after init.
- All tasks document their stack size; check high-water marks in debug builds.
- Anything BLE-facing gets a host-side unit test that runs on desktop.
```

### 14.6 Phone project context — `CLAUDE.md` at the Android repo root

```markdown
# Project: Slate companion — Android bridge, script host and app repository client

## What this app is
Three things: a BRIDGE (adapts notifications, media, navigation, health from other apps),
a HOST (runs downloaded JS sub-apps in a sandbox), and a CLIENT (pushes display lists to
the watch over BLE, receives input events).

## Hard rules
- Sub-apps are DOWNLOADED JAVASCRIPT, never dex/JAR/.so. Play policy permits interpreted
  code loaded at runtime; it prohibits downloading executable code. Do not blur this line.
- Sub-app scripts run in androidx.javascriptengine (V8, isolated process). Bindings are
  permission-gated and whitelisted. No reflection, no filesystem, no raw Android APIs.
- The app MUST keep working against the oldest firmware ever shipped. Sub-apps declare
  minProtocolVersion; the compositor blocks with an explanation rather than failing at render.
- The Kotlin DSL and the JS builder must emit BYTE-IDENTICAL display lists. Golden-file
  tests enforce this.

## Stack
Kotlin, coroutines, Jetpack Compose UI. minSdk 30. Raw android.bluetooth (no BLE wrapper
library — I want the actual calls visible). androidx.javascriptengine for scripts.
CompanionDeviceManager with the `watch` device profile.

## Non-negotiable Android specifics
- Foreground service with foregroundServiceType="connectedDevice" (required since Android 15
  for background BLE).
- CDM association + CompanionDeviceService + startObservingDevicePresence() for reconnect.
- NotificationListenerService needs a system-settings deep link; it cannot be granted normally.
- Expect Samsung/Xiaomi/Huawei battery optimisers to kill the service; detect and explain.

## Protocol
SDP, defined in slate-implementation-roadmap.md (§4). Encoder must match the firmware exactly.
```

---

---

# Part III — The prompts

Eighteen prompts in six phases. Read section 14.4 first and create both context files before running any of them.

## Phase 0 — Foundations

### M0 — Toolchain, blinky, RTT logging

| | |
|---|---|
| **Phase** | Phase 0 |
| **Goal** | Board boots, flashes, logs; the toolchain is trustworthy |
| **Touches** | CMakeLists.txt, linker script, startup, UICR, RTT |
| **Depends on** | — |
| **Rough size** | 3-5 days |

**Acceptance:** `.hex` builds and flashes; motor pulses; RTT shows a boot banner.

```
Set up a bare-metal C++17 project for the Nordic nRF52832 on the PineTime, using CMake
and arm-none-eabi with the nRF5 SDK (or bare CMSIS — give me the trade-off and pick one).

Deliver:
1. CMakeLists.txt producing a flashable .hex, with debug (RTT logging) and release builds.
2. A linker script with a BOOTLOADER_PRESENT option: when OFF, the app links at 0x0 so I
   can flash and run it standalone during bring-up; when ON, it links at 0x8000 to sit
   above a 32KB MCUBoot. Default OFF for now. Document .data/.bss and the stack/heap split.
3. Startup code, vector table, SystemInit enabling the DC/DC regulator and the 32.768kHz LFXO.
4. UICR configuration setting NFCPINS so P0.09/P0.10 are GPIO — P0.10 is the touch reset,
   so this is required. Explain how the UICR write is applied and why it needs a power cycle.
5. SEGGER RTT logging with a compile-time level switch.
6. main() pulsing the vibration motor three times and logging a boot banner.
7. An OpenOCD config and documented flash command for an ST-Link v2.

Show every register write explicitly. Do not write "configure the clock" — write the code.
```

### M1 — Display driver and tile renderer

| | |
|---|---|
| **Phase** | Phase 0 |
| **Goal** | Pixels on screen through a dirty-rect tile renderer |
| **Touches** | ST7789 driver, SPI bus arbiter, Renderer, backlight PWM |
| **Depends on** | M0 |
| **Rough size** | 8-12 days |

**Acceptance:** renders a gradient, shapes and text; reports per-frame timing; full-screen redraw within ~15% of the 115 ms theoretical floor.

```
Write an ST7789 driver and tile-based renderer for the PineTime.

Hardware (authoritative): SPI mode 3, max 8MHz, SCK=P0.02, MOSI=P0.03, DC=P0.18,
CS=P0.25, RST=P0.26, backlight active-low. 240x240 RGB565. The external flash shares
this SPI bus (CS=P0.05), so the driver must cooperate on bus ownership.

Constraints:
- NO full framebuffer — 240*240*2 = 115,200 bytes and we have 64KB of RAM.
- Render into TWO 240x8 RGB565 tile buffers, 3,840 bytes each, 7,680 bytes total.
  DMA one out via SPIM while drawing into the other. Tile height is a compile-time
  constant so I can retune it — show me the RAM cost as a function of that constant.
- Note the physics: a full-screen redraw is 115,200 bytes at 8Mbit/s = ~115ms floor,
  about 8fps. Dirty-rectangle rendering is therefore mandatory. Design for it from
  the start rather than adding it later.

Deliver:
1. ST7789 init with correct mode-3 timing, plus SLPIN/SLPOUT for power management.
2. A bus-arbitration wrapper (mutex + CS management) that the flash driver will also use.
3. A Renderer with fillRect, drawLine, drawCircle, drawRoundRect, drawArc, and
   blitBitmap for RGB565, RGB332 and 1-bit sources.
4. A dirty-rectangle tracker so only changed tiles are transmitted, with a clear API for
   marking regions dirty.
5. Backlight PWM control, 0-100 mapped onto the three backlight pins.
6. A demo drawing a gradient, three rectangles and a circle, reporting render and
   transmit time per frame over RTT.

Report the measured full-screen redraw time and the measured time for a 6-tile partial
update, and tell me where the bottleneck actually is.
```

### M2 — Touch, button, gestures

| | |
|---|---|
| **Phase** | Phase 0 |
| **Goal** | Touch, button and gestures as structured events |
| **Touches** | TWI wrapper, CST816S driver, button strobe, hit-test |
| **Depends on** | M0 |
| **Rough size** | 5-8 days |

**Acceptance:** taps, swipes and button presses emitted as structured events; hit-test unit-tested on host.

```
Write a CST816S touch driver and input event layer for the PineTime.

Hardware (authoritative): CST816S at I2C 0x15, SDA=P0.06, SCL=P0.07, RST=P0.10, IRQ=P0.28.
The controller SLEEPS when idle and stops responding on the bus entirely — this is normal
behaviour, not a fault, and the driver must tolerate it. Historically, writing the sleep
command wedged the controller until the battery drained, so do NOT implement sleep-mode
writes. All TWI operations need timeouts and bus recovery.
Touch data occupies the first 63 registers. Byte 1 is a gesture ID:
0x01 slide-down, 0x02 slide-up, 0x03 slide-left, 0x04 slide-right,
0x05 single-tap, 0x0B double-tap, 0x0C long-press.
The controller reports only one touch point in practice.
The button requires P0.15 driven high to read P0.13 and costs 34uA if left high —
strobe it: set high, take 4 stable reads, read, set low.

Deliver:
1. A TWI wrapper with timeout and bus recovery (clock out stuck slaves).
2. A CST816S driver, interrupt-driven off P0.28, tolerant of the controller sleeping.
3. A button driver using the strobe technique, with debounce and press/long/double detection.
4. An InputEvent layer producing TAP(x,y), LONG_PRESS(x,y), SWIPE(dir), BUTTON(action).
5. A hit-test helper mapping a tap to an element id, given a list of {id,x,y,w,h} rects.
6. A demo drawing touch coordinates on screen and logging gestures over RTT.

Include a host-side unit test for the hit-test logic, including overlapping and
zero-area rectangles.
```

---

## Phase 1 — Vertical slice

### M3 — Display-list interpreter

| | |
|---|---|
| **Phase** | Phase 1 |
| **Goal** | Render an untrusted display list without ever faulting |
| **Touches** | Interpreter, validation helpers, retained list, fuzz harness |
| **Depends on** | M1 |
| **Rough size** | 8-12 days |

**Acceptance:** renders hardcoded lists; fuzzer runs clean; malformed input never faults.

```
Implement the SDP display-list interpreter for the PineTime firmware.
Opcode table: [paste §4.3]
Enumerations and flag fields: [paste §4.5]
Extension opcode rules: [paste §7.2]

Critical requirements:
- This parser consumes UNTRUSTED data from BLE. Validate EVERY field before use:
  coordinates 0-239, lengths within the remaining buffer, font/atlas/element IDs within
  registered ranges. On any violation: abort the list, log, leave the previous frame
  intact. NEVER fault, never read out of bounds.
- Caps: 512 ops per list, 4KB per list, 30ms total parse+render.
- Implement the COLOR and STYLE operand types EXACTLY as specified in §4.3. COLOR is
  variable length (1 byte for a palette reference, 3 for a literal RGB565), so the
  operand walker must be length-aware rather than assuming fixed-size operands. Reject
  COLOR tags 0x11-0xFF and STYLE mode 3.
- Build SHARED validation helpers FIRST — coordinate bounds, colour decode, length-remaining
  checks, ID-range checks — and use them in every opcode handler. Do NOT hand-roll bounds
  checks per opcode. The base set is deliberately large so the phone rarely needs PATCH, and
  that is only safe if validation is centralised rather than repeated 20 times.
- Extension opcodes 0xE0-0xEF are length-prefixed (u16 after the opcode). Unknown ones
  must be SKIPPED by advancing 3+length, not treated as an error. This is the
  forward-compatibility mechanism — get it exactly right.
- Maintain a 4KB retained-list buffer holding the current screen so the watch can
  re-render locally on wake or scroll with no phone round-trip.
- BEGIN_ELEM/END_ELEM build the hit-test table used by M2's input layer.
- SCROLL_REGION: the watch owns scroll offset locally and re-renders from the retained
  list. No round trip.

Deliver:
1. A libFuzzer harness for the parser that builds and runs on the host — SHOW ME THIS FIRST,
   before the implementation.
2. The interpreter, rendering through the M1 Renderer.
3. Host-side unit tests covering every opcode and, especially, malformed input: truncated
   ops, out-of-range coords, bad IDs, oversized strings, op-count overflow, unknown
   extension opcodes with valid and invalid lengths.
4. A demo rendering three hardcoded lists (clock face, notification, navigation screen)
   with parse+render timing for each.
```

### M4 — Display-list DSL and desktop emulator

| | |
|---|---|
| **Phase** | Phase 1 |
| **Goal** | Design and test watch UI with no hardware at all |
| **Touches** | Kotlin DSL, desktop emulator, golden-file pixel tests |
| **Depends on** | M3 (byte-compat only) |
| **Rough size** | 8-12 days |

**Acceptance:** Kotlin DSL and firmware interpreter agree pixel-for-pixel on a golden-file suite.

Build this **before** BLE. It pays for itself repeatedly.

```
Build two things so I can design watch UI without hardware.

1. A Kotlin DSL producing SDP display-list byte arrays.
   Opcode table: [paste §4.3]   Enumerations and flag fields: [paste §4.5]
   Target API:
     displayList {
       palette(0, Color.BLACK); palette(1, Color.WHITE)
       clear(pal(0))
       text(font = LARGE, x = 120, y = 100, align = CENTER,
            color = pal(1), text = "12:34")
       element(id = 1, x = 0, y = 200, w = 240, h = 40) {
         rectRound(0, 200, 240, 40, r = 8, color = rgb(0x4208), style = Style.FILL)
       }
       commit()
     }

   Implement COLOR as a sealed type with two cases — Rgb565(value) encoding as
   [0x00, lo, hi], and Pal(index) encoding as a single byte index+1. Implement STYLE
   as a value class packing mode into bits 0-1 and stroke width into bits 2-5.
   Both encodings are specified in §4.3; the firmware, this DSL and the JS builder
   must agree byte-for-byte, so put the constants in ONE shared definition that all
   three consume rather than transcribing them.

2. A desktop emulator (Compose Desktop) taking a display-list byte array and rendering
   it to a 240x240 canvas with the SAME font metrics and colour handling as the watch,
   plus simulated tap input emitting SDP input events.

Deliver both plus a golden-file test suite: display lists with expected PNG renders, so
I can verify emulator and firmware agree pixel-for-pixel.

Font metrics are the risky part. Propose a design where the emulator and the firmware
consume LITERALLY THE SAME font data file, so they cannot diverge — I do not want two
implementations of text layout.
```

### M5 — BLE peripheral, GATT, framing

| | |
|---|---|
| **Phase** | Phase 1 |
| **Goal** | BLE transport, GATT surface and SDP framing |
| **Touches** | NimBLE integration, GATT server, framing, reassembly |
| **Depends on** | M0 |
| **Rough size** | 8-12 days |

**Acceptance:** phone echoes bytes through the watch; negotiated MTU/PHY/DLE logged.

```
Implement the BLE layer for Slate on the PineTime using NimBLE on FreeRTOS.

Deliver:
1. NimBLE integration with a tuned mbuf pool. Target under 6KB — show me the config and
   the reasoning, and if 6KB is infeasible for a single connection at MTU 247 with DLE,
   tell me the real minimum and why. Report actual measured RAM usage.
2. GATT server exposing:
   - Slate service: RX (write-no-response), TX (notify), STATUS (read/notify)
   - Standard: Current Time Service (as client), Battery, Heart Rate, Device Information
   Generate a 128-bit base UUID and tell me what it is.
3. Connection parameter management: negotiate ATT MTU 247, then Data Length Extension,
   then 2M PHY, then a connection interval appropriate to state. All four matter —
   without DLE we are capped near 54 kB/s. Log what was ACTUALLY negotiated, since
   phones frequently refuse.
4. The SDP framing layer: [paste §4.2]
   - 2-byte header, 8 channels, per-channel sequence numbers
   - Reassembly of multi-packet messages, 4KB bounded reassembly buffer
   - Malformed frames dropped and the channel resynchronised, never a fault
5. A loopback test mode: bytes written to RX on channel 7 return on TX.
6. Host-side unit tests for framing: fragmentation, sequence wrap, out-of-order,
   truncated frames, oversized length fields.

Transport only — no display or input channels yet.
```

### M6 — First phone-pushed screen

| | |
|---|---|
| **Phase** | Phase 1 |
| **Goal** | The vertical slice: a phone pushes a screen to the watch |
| **Touches** | Android app skeleton, CDM, foreground service, GATT client |
| **Depends on** | M3, M4, M5 |
| **Rough size** | 6-10 days |

**Acceptance:** an Android app pushes a clock display list; the watch renders it; RTT and negotiated parameters visible on the phone.

```
Build a minimal Android app that connects to the Slate watch and pushes a display list.

Requirements:
1. CompanionDeviceManager association using the `watch` device profile, plus
   CompanionDeviceService and startObservingDevicePresence() for reconnect.
2. A foreground service with foregroundServiceType="connectedDevice" (required since
   Android 15) owning the GATT connection, with automatic reconnection.
3. GATT client: discover the Slate service, negotiate MTU 247 / DLE / 2M PHY, subscribe
   to TX notifications. Log what was actually granted — phones often refuse requests.
4. The SDP framing encoder, matching the firmware exactly: [paste §4.2]
5. Use the M4 Kotlin DSL to build and push a clock screen updating once per second.
6. An on-screen readout of negotiated MTU, PHY, connection interval and measured RTT.

Kotlin, coroutines, minSdk 30. Use raw android.bluetooth — no third-party BLE library.
I want the actual calls visible so I understand what is happening.
```

### GATES — Measurement gates A–D

| | |
|---|---|
| **Phase** | Phase 1 |
| **Goal** | Measure throughput, latency, power and render time before freezing SDP |
| **Touches** | Firmware benchmark mode, Android benchmark screen, PPK II run |
| **Depends on** | M6 |
| **Rough size** | 3-5 days |


```
Write a throughput, latency and render benchmark harness for Slate, in two parts.

Firmware: a benchmark mode that (a) receives N bytes on channel 7 and reports wall-clock
time and effective kB/s, (b) echoes timestamped packets for RTT, (c) reports NimBLE mbuf
high-water mark, (d) reports parse+render time for a supplied display list.

Android: a benchmark screen running a sustained transfer, a 1000-sample RTT test, and a
render-timing test, reporting mean/p50/p95/p99 plus negotiated MTU/PHY/interval.

Then tell me how to interpret the results against these thresholds, and SPECIFICALLY what
to change in the protocol design if any fails:
- Sustained throughput >= 60 kB/s
- Tap-to-response p95 < 250 ms
- Parse + render of a typical 80-byte list < 30 ms
```

Run gate C (power) separately with a Nordic Power Profiler Kit II across the four states in §8.

---

## Phase 2 — Protocol hardening

### M7 — Input path and session state machine

| | |
|---|---|
| **Phase** | Phase 2 |
| **Goal** | Interaction that tolerates latency, and a session lifecycle |
| **Touches** | Input channel, session state machine, negotiable power profiles |
| **Depends on** | M2, M3, M6 |
| **Rough size** | 6-9 days |


```
Implement the SDP input channel and session state machine in the PineTime firmware.

Input channel (channel 2): [paste §4.4]
Enumerations and flag fields: [paste §4.5]
Design point: the watch owns scroll position, selection focus and back-navigation WITHIN
a pushed screen — zero round trip. Only discrete semantic events cross the link. Give
instant local haptic and visual feedback on touch, before the phone responds.

Session state machine: [paste §4.6]
- Negotiation reporting firmware/protocol version, geometry, a 32-byte supported-opcode
  bitmap, free display-list bytes, supported session profiles, and asset-pack hashes
- Heartbeat every 2s; 2 missed = staleness overlay; 5 missed = pop remote stack
- Remote screen stack with push/pop/replace, local watch face beneath

Session profiles must be NEGOTIABLE, not hardcoded: the phone requests a named profile
(e.g. "ambient", "active", "streaming") and the watch applies the matching backlight and
connection-interval policy. Be precise about ownership: the SET of profiles and what each
one means is FIRMWARE-OWNED and reported during negotiation. The phone SELECTS among them;
it cannot define one or override the parameters. So adding a new profile is a firmware
change, but a new app category choosing a different existing profile is not.

Deliver the firmware plus host-side state machine tests: clean connect, negotiation
failure, mid-session link loss, reconnect with same phone, reconnect with a different phone.
```

### M8 — Compositor and app host (Android)

| | |
|---|---|
| **Phase** | Phase 2 |
| **Goal** | Screen arbitration and a process-boundary-safe app host |
| **Touches** | Compositor, priority classes, per-app quotas, app interface |
| **Depends on** | M6 |
| **Rough size** | 8-12 days |


```
Build the Compositor and app host for the Slate Android app.

Requirements:
1. A screen stack with priority arbitration. Classes: AMBIENT, NORMAL, INTERRUPT
   (call, alarm), CRITICAL. Document the focus-stealing rules — when may an app steal
   focus, and what happens to the one it displaced?
2. Input event routing to the focused app, with a fallback for unhandled events.
3. Render scheduling: apps declare a refresh policy (on-change, periodic, manual). The
   compositor coalesces and rate-limits so we never exceed the credit window or wake
   the radio unnecessarily.
4. Credit-based flow control against the watch's advertised free buffer.
5. Per-app push quotas (10/s foreground, 1/min ambient) enforced here.
6. minProtocolVersion gating: refuse to focus an app whose requirement exceeds the
   connected watch's reported protocol version, and show an "update your watch" screen.
7. A no-op TestApp and a ClockApp as reference implementations.

CRITICAL DESIGN CONSTRAINT: sub-apps will shortly be DOWNLOADED JAVASCRIPT running in an
isolated process (androidx.javascriptengine), not compiled-in Kotlin. So define the app
interface as a PROCESS-BOUNDARY-SAFE contract now: serializable display lists, no shared
mutable state, explicit lifecycle, async everywhere, no passing of Android objects.
Show me the interface, and show me how a JS implementation and a Kotlin implementation
would both satisfy it.
```

### M9 — Adapters and the first real app

| | |
|---|---|
| **Phase** | Phase 2 |
| **Goal** | The first real bridge and the first real app |
| **Touches** | NotificationListenerService, icon mapping, Notifications app |
| **Depends on** | M7, M8 |
| **Rough size** | 8-12 days |


```
Implement the notification adapter and a Notifications app for Slate.

1. NotificationListenerService capturing posted and removed notifications: filtering by
   package, deduplication, group/summary handling, extraction of title/text/icon/actions.
2. Map notification icons to our on-watch icon atlas with a fallback glyph. Explain your
   approach to the icon problem — we cannot ship every app's icon, and the watch atlas
   is a runtime asset with limited space.
3. A Notifications app implementing the M8 interface: a scrollable list (using
   SCROLL_REGION so scrolling is watch-local), a detail screen, and action buttons
   mapping to notification actions (canned replies, dismiss, snooze).
4. INTERRUPT-priority focus request on high-priority notifications, with a
   user-configurable per-package filter for what may interrupt.
5. Push notifications to the watch's local retained store (channel 4, SYSTEM) so they
   remain readable when the phone is away.

Handle the NotificationListenerService permission flow properly — it needs a system
settings deep link and cannot be granted through the normal runtime permission path.
```

---

## Phase 3 — Resilience and assets

### M10 — The resilient local core

| | |
|---|---|
| **Phase** | Phase 3 |
| **Goal** | The watch behaves like a watch with no phone present |
| **Touches** | RTC clock, step counter, alarm scheduler, local screens, tilt-to-wake |
| **Depends on** | M1, M2, M7 |
| **Rough size** | 10-15 days |


```
Implement the local resilient core for Slate firmware — everything working with no phone.
Spec: [paste §3.4]

Deliver:
1. Timekeeping: RTC-backed clock, drift correction applied on each Current Time Service
   sync, surviving reboot.
2. Step counting from BMA421/BMA425 — detect the variant at runtime (devices before
   July 2021 have the 421). Use the sensor's OWN step-counter feature rather than
   processing raw acceleration, to minimise wake-ups. Target under 20uA average.
3. Alarms and timers firing with no phone: a scheduler surviving reboot, backed by
   persistent storage, driving haptics and a local alert screen.
4. Local screens: watch face (time, date, battery, steps, link status), notification
   list from the retained store, settings, charging.
5. Tilt-to-wake from the accelerometer interrupt, configurable sensitivity.
6. Staleness handling: on link loss, mark retained remote screens stale after 2 missed
   heartbeats, pop them after 5, revealing the watch face.

RAM constraint: fit within the 3KB local-screen-state and 4KB notification-store budgets.
Report actual usage against those numbers.
```

### M11 — Asset packs and LittleFS

| | |
|---|---|
| **Phase** | Phase 3 |
| **Goal** | Fonts and icons become runtime data, not firmware |
| **Touches** | LittleFS, asset pack format, glyph cache, ASSET channel, pack tool |
| **Depends on** | M1, M3 |
| **Rough size** | 10-14 days |


```
Implement asset pack storage and the glyph/icon cache for Slate firmware.

Context: fonts and icon atlases live in LittleFS on the 4MB external SPI NOR, which
SHARES the SPI bus with the display — coordinate through the M1 bus arbiter. The RAM
working-set cache is capped at 6KB.

Deliver:
1. LittleFS over the XT25F32B driver, with deep-power-down support (CS high, low, write
   0xB9, CS high) for the idle path.
2. An asset pack format: header with version and hash, plus an index of fonts (with
   metrics) and icon atlases, designed for direct indexed access WITHOUT loading the
   whole pack. Unknown record types must be skippable by length — this is a
   forward-compatibility requirement, same principle as the extension opcodes.
3. A glyph cache: LRU over individual glyphs paged from flash, 6KB budget. Report the
   cache hit rate rendering a text-heavy screen.
4. The ASSET channel (channel 3) receiver: chunked, resumable, hash-verified,
   flow-controlled, strictly yielding to DISPLAY and INPUT so a font transfer never
   makes the UI stutter.
5. A desktop tool building asset packs from TTF fonts and SVG/PNG icons, emitting the
   pack PLUS the constants consumed by the firmware, the Kotlin DSL and the JS builder —
   one source of truth so the emulator and the watch cannot diverge.

The asset pack index parser also consumes untrusted data. Bound it exactly like the
display-list parser, and fuzz it.
```

---

## Phase 4 — The script platform

### M12 — Script runtime and sandbox

| | |
|---|---|
| **Phase** | Phase 4 |
| **Goal** | Sub-apps run as sandboxed, permission-gated scripts |
| **Touches** | javascriptengine sandbox, bindings, Governor, JS builder, dev console |
| **Depends on** | M4, M8 |
| **Rough size** | 15-20 days |


```
Build the scripted sub-app runtime for the Slate Android app.

Sub-apps are DOWNLOADED JAVASCRIPT bundles, not compiled-in code. Google Play permits
interpreted code loaded at runtime; it prohibits downloading executable code (dex/JAR/.so).
Do not blur that line anywhere in this implementation.

Requirements:
1. Use androidx.javascriptengine (V8 in an isolated process exclusive to our app). Justify
   the isolate lifecycle: one isolate per running sub-app, or a shared isolate? Measure
   the IPC round-trip cost of a render() call and report it — if it exceeds 20ms, tell me
   and propose QuickJS in-process as the fallback with a whitelisted binding surface.
2. Implement the sub-app lifecycle: onFocus, render, onInput, onEvent, onBlur — satisfying
   the process-boundary-safe interface from M8.
3. Implement the permission-gated binding surface: [paste §6.3 table]
   Bindings must be whitelisted explicitly. No reflection, no filesystem, no raw Android
   objects crossing the boundary. A script must never obtain more permission than the host
   app holds.
4. A JS display-list builder emitting BYTE-IDENTICAL output to the M4 Kotlin DSL. Add
   cross-implementation golden-file tests that fail if they ever diverge.
5. The Governor enforcing: [paste §6.5 table]
   Kill on overrun, disable after repeated violations, surface diagnostics to the user.
6. An in-app developer console showing slate.log output, execution timings, quota usage
   and violations — sub-app authors will need this.
7. Wire the M4 desktop emulator to run scripts too, so a sub-app can be developed with
   NO watch and NO phone. Document that workflow.

Deliver the runtime plus a sample sub-app (a timer) exercising render, input, timers
and persistence.
```

### M13 — Repository client and installer

| | |
|---|---|
| **Phase** | Phase 4 |
| **Goal** | Sub-apps arrive from a repository, not a Play release |
| **Touches** | Package format, Ed25519 signed index, installer UI, multi-repo trust |
| **Depends on** | M12 |
| **Rough size** | 12-18 days |


```
Build the sub-app repository client for the Slate Android app.

Model: a static, signed JSON index over HTTPS — no server-side component. Bangle.js's
app loader is the reference.

Requirements:
1. Package format: [paste §6.2] Implement parsing and validation of manifest.json,
   including rejecting unknown-but-required fields and enforcing permission declarations.
2. Index format: [paste §6.6] Fetch, cache, and parse.
3. Signing: verify an Ed25519 signature over the index; verify each package against its
   SHA-256 from the index. One trusted key. Explain key rotation and what happens if the
   key is compromised. (Note: firmware signing uses ECDSA-P256 for MCUBoot — different
   trust domain, do not share keys or code paths.)
4. Browse/install/update/remove UI in Compose: list, detail with screenshots, permission
   disclosure BEFORE install, installed-apps management.
5. Multiple repositories: users may add URLs. Show provenance clearly. Third-party repos
   get a REDUCED permission set (no http, health or location) unless the user grants per-app.
   A third-party repo must never shadow an official app ID.
6. Update policy: scheduled index checks, respect metered-connection settings, and NEVER
   auto-install a version that increases the permission set — require explicit consent.
7. Offline: installed packages are cached locally. Installing requires network; RUNNING an
   installed sub-app must not.
8. minProtocolVersion / minHostVersion filtering: show apps the connected watch cannot run
   as clearly unavailable with the reason, rather than hiding them.

Also draft the content policy document for the official repository. We are responsible
under Play policy for what our app can be made to do, so I need a takedown process and
review criteria.
```

---

## Phase 5 — Production

### M14 — Power optimisation

| | |
|---|---|
| **Phase** | Phase 5 |
| **Goal** | The power targets in section 8 are met and verified |
| **Touches** | Tickless idle, peripheral discipline, interval renegotiation |
| **Depends on** | M7, M10 |
| **Rough size** | 8-12 days |


```
Optimise Slate firmware to the §8 power targets: ambient <200uA average, active session
20-25mA. Verify with measurements.

Work through in order:
1. FreeRTOS tickless idle with the LFXO as tick source.
2. Peripheral shutdown discipline: SPI and TWI disabled when idle; SAADC STOPPED (not
   merely disabled) after each battery sample; HRS3300 in sleep (PDRIVER=0) unless
   measuring; external flash in deep power-down; LCD in SLPIN when the backlight is off.
3. Button strobing instead of leaving P0.15 high (saves 34uA).
4. Pin-sense rather than edge-triggered GPIOTE where possible — edge-triggered interrupts
   have been measured costing up to 0.47mA in some configurations.
5. Connection interval renegotiation on every power-state transition, driven by the
   negotiated session profile from M7.
6. Verify the debug peripheral is OFF when measuring — it costs ~3mA and stays enabled
   after a debugger disconnects until power cycle. Document the disable procedure.

Deliver the optimisations plus a power-state instrumentation build logging transitions
with timestamps, so I can correlate a PPK II trace against behaviour. Give me a table of
measured current per state, and tell me which single item moved the needle most.
```

### M15 — OTA, MCUBoot, recovery

| | |
|---|---|
| **Phase** | Phase 5 |
| **Goal** | Updates are safe and a sealed watch is always recoverable |
| **Touches** | MCUBoot layout, ECDSA signing, OTA channel, Nordic DFU recovery |
| **Depends on** | M0, M5 |
| **Rough size** | 10-15 days |


**Read every line. This is where a mistake bricks a sealed watch.**

```
Implement firmware update for Slate. Safety-critical: the sealed PineTime has no
accessible SWD, so a bad update is an unrecoverable brick for the user.

Requirements:
1. MCUBoot with PRIMARY slot in internal flash and SECONDARY slot in EXTERNAL SPI flash
   (reclaims ~200KB internally). Give me the flash layout as a table with explicit
   addresses and sizes, and switch the M0 linker script to BOOTLOADER_PRESENT=ON.
2. ECDSA-P256 image signing, public key in internal flash. Document the signing workflow
   and private key handling.
3. Swap-with-revert: if the new image fails to confirm within N boots, MCUBoot reverts.
   Define "confirm" as REQUIRING A SUCCESSFUL BLE CONNECTION, not merely a successful
   boot — firmware that boots but cannot connect is otherwise unrecoverable.
4. OTA channel (channel 5) receiver: chunked, resumable, hash-verified, refusing to start
   below 30% battery.
5. Nordic legacy DFU as an INDEPENDENT recovery path so nRF Connect, Gadgetbridge or
   Amazfish can reflash if our own app or protocol is broken. This is the deliberate
   safety net — do not skip or "simplify" it.
6. A documented recovery procedure for each failure mode: bad image, interrupted transfer,
   image that boots but cannot connect, corrupted bootloader.

Enumerate every way this can brick a sealed device and what specifically defends against each.
```

### M16 — Navigation and camera sub-apps

| | |
|---|---|
| **Phase** | Phase 5 |
| **Goal** | The actual product, demonstrated as downloadable sub-apps |
| **Touches** | Navigation sub-app, Camera sub-app, generic nav adapter |
| **Depends on** | M9, M11, M13 |
| **Rough size** | 10-15 days |


```
Implement two SCRIPTED sub-apps exercising the remaining protocol surface. Both must be
JavaScript bundles installed through the M13 repository client, not compiled-in code —
this validates that the platform actually works end to end.

A) Navigation — the flagship use case.
   - Source maneuvers from OsmAnd's intent API via a generic nav adapter binding, so
     other nav apps can plug in later.
   - Screen: maneuver icon, distance, street name, progress arc, ETA.
   - Push ONE display list per maneuver change, NOT a periodic refresh. This is a 30+
     minute session and radio time is the battery cost.
   - Define behaviour when GPS is lost or the phone disconnects mid-route.

B) Camera — proves the patch tier.
   - CameraX with a small analysis target; downscale to 60x60 and convert to RGB332
     ON THE PHONE (3.6KB per frame, ~10fps achievable). Tap on the watch fires the shutter.
   - A frame-rate governor adapting to measured throughput, DROPPING frames rather than
     queuing them.
   - Note this needs a camera binding in the script API that does not exist yet — design
     it, and tell me whether it belongs in the script surface at all or should be a
     privileged built-in.

For both, report measured watch battery cost per minute of session against the
6%-per-30-minutes budget in §8.
```

---


---

## Appendix A — Prompt index and dependencies

Every prompt, its dependencies and its size, in one place. Use this to plan parallel work and to check nothing is started before its inputs exist.

| Prompt | Phase | Goal | Depends on | Rough size |
|---|---|---|---|---|
| **M0** | Phase 0 | Board boots, flashes, logs; the toolchain is trustworthy | — | 3-5 days |
| **M1** | Phase 0 | Pixels on screen through a dirty-rect tile renderer | M0 | 8-12 days |
| **M2** | Phase 0 | Touch, button and gestures as structured events | M0 | 5-8 days |
| **M3** | Phase 1 | Render an untrusted display list without ever faulting | M1 | 8-12 days |
| **M4** | Phase 1 | Design and test watch UI with no hardware at all | M3 (byte-compat only) | 8-12 days |
| **M5** | Phase 1 | BLE transport, GATT surface and SDP framing | M0 | 8-12 days |
| **M6** | Phase 1 | The vertical slice: a phone pushes a screen to the watch | M3, M4, M5 | 6-10 days |
| **Gates A–D** | Phase 1 | Measure throughput, latency, power and render time before freezing SDP | M6 | 3-5 days |
| **M7** | Phase 2 | Interaction that tolerates latency, and a session lifecycle | M2, M3, M6 | 6-9 days |
| **M8** | Phase 2 | Screen arbitration and a process-boundary-safe app host | M6 | 8-12 days |
| **M9** | Phase 2 | The first real bridge and the first real app | M7, M8 | 8-12 days |
| **M10** | Phase 3 | The watch behaves like a watch with no phone present | M1, M2, M7 | 10-15 days |
| **M11** | Phase 3 | Fonts and icons become runtime data, not firmware | M1, M3 | 10-14 days |
| **M12** | Phase 4 | Sub-apps run as sandboxed, permission-gated scripts | M4, M8 | 15-20 days |
| **M13** | Phase 4 | Sub-apps arrive from a repository, not a Play release | M12 | 12-18 days |
| **M14** | Phase 5 | The power targets in section 8 are met and verified | M7, M10 | 8-12 days |
| **M15** | Phase 5 | Updates are safe and a sealed watch is always recoverable | M0, M5 | 10-15 days |
| **M16** | Phase 5 | The actual product, demonstrated as downloadable sub-apps | M9, M11, M13 | 10-15 days |

**Reading the dependency column.** A dependency means the named prompt must be *complete and verified*, not merely started. The one soft dependency is M4 on M3: the emulator can be built in parallel, but its output must be byte-identical to what the M3 interpreter expects, so the opcode encoding has to be settled first.

**Critical path:** M0 → M1 → M3 → M6 → Gates → M8 → M12 → M13 → M16. Everything not on that path can absorb slippage without moving the release.


---

## Sources

- [PineTime — PINE64 wiki](https://wiki.pine64.org/wiki/PineTime) — pinout, display/touch specifics, power table, battery formula, gesture IDs
- [PineTime Development — PINE64 wiki](https://wiki.pine64.org/wiki/PineTime_Development) — battery-friendly guidance, debug peripheral behaviour
- [PineTime SD MCUBoot — PINE64 wiki](https://wiki.pine64.org/wiki/PineTime_SD_MCUBoot) — bootloader layout constraints
- [InfiniTime memory analysis](https://github.com/InfiniTimeOrg/InfiniTime/blob/main/doc/MemoryAnalysis.md) — the 60,416 B figure calibrating §3.2
- [InfiniTime BLE documentation](https://github.com/InfiniTimeOrg/InfiniTime/blob/develop/doc/ble.md) — UUID scheme convention
- [Device and Network Abuse — Play Console Help](https://support.google.com/googleplay/android-developer/answer/16559646) — the executable-code prohibition and the interpreter exemption
- [Dynamic Code Loading — Android Developers](https://developer.android.com/privacy-and-security/risks/dynamic-code-loading)
- [JavaScriptEngine — Jetpack](https://developer.android.com/jetpack/androidx/releases/javascriptengine) and [Executing JavaScript and WebAssembly](https://developer.android.com/develop/ui/views/layout/webapps/jsengine) — V8 in an isolated process
- [Companion device pairing — Android Developers](https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing) and [Companion device profiles — AOSP](https://source.android.com/docs/core/connect/companion-device-profile)
- [Communicate in the background — Android Developers](https://developer.android.com/develop/connectivity/bluetooth/ble/background)
- [Android 15 BLE permission changes](https://bleadvertiserapp.medium.com/android-15-broke-your-ble-app-new-permission-rules-3d8cb3c9ba86)
- [A Practical Guide to BLE Throughput — Memfault](https://interrupt.memfault.com/blog/ble-throughput-primer) and [Punch Through](https://punchthrough.com/ble-throughput-part-4/)
- [nRF52832 Product Specification](https://www.mouser.com/datasheet/2/297/nRF52832_PS_v1_8-2942485.pdf)
- [MCUBoot](https://github.com/mcu-tools/mcuboot)
- [Bangle.js App Loader](https://espruino.github.io/BangleApps/) — the static signed-index repository model
- [Gadgetbridge Intent API](https://gadgetbridge.org/internals/automations/intents/)