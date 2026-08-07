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
- Button: P0.15 enable + P0.13 sense (active-high, pulldown). Match InfiniTime: leave
  enable high (~34 µA). Do not invent a strobe-only scheme that breaks WDT-hold reset.
- Battery: ADC AIN7 (P0.31), 1:2 divider. Sample as InfiniTime does — SAADC
  10-bit, gain 1/4, internal 0.6V ref (full scale 2.4V pin = 4.8V battery) —
  then mV = raw * 8 * 600 / 1024. The config and the formula are one unit:
  change `power.cpp::sample_battery_adc` and `battery.cpp::millivolts`
  together. (The old `adc * 2000 / 1241` matched no working config; see N-12.)
  Charge indicator P0.12 (low = charging).
- NFC unavailable: P0.10 is the touch reset, so UICR NFCPINS must select GPIO.

## Stack
FreeRTOS + NimBLE + LittleFS + MCUBoot. C++17. No LVGL — custom tile renderer.

**Task topology (current):** one FreeRTOS **app** task (former main loop: UI, WDT,
session/core tick, SDP drain) plus NimBLE **ll** / **ble** host tasks and the
FreeRTOS timer daemon. GATT RX only reassembles + publishes into `ble::AppInbox`
(zero-copy: the inbox borrows the reassembler buffer; ingest is gated while a
message is pending, CREDIT withheld until apply);
the app task drains (`Link::drain_app_messages`) and owns interpreter/renderer —
InfiniTime-style link→app handoff (N-1 / I-10 stage 1). Full roadmap §3.1
`display` / `link` / `sensors` / `system` split remains **deferred**. M5a
scheduler proof is `freertos_smoke` (2-task + queue, then self-delete).

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
- SDP wire constants (opcodes, COLOR/STYLE, enums, flags) live in **one** file:
  `include/sdp_opcodes.hpp`. Normative spec is slate-implementation-roadmap.md §4.
  Do not duplicate those values elsewhere.

## Architecture
Thin client. The phone pushes display lists; the watch renders and returns element-level
input events. A resilient local core (watch face, steps, alarms, retained screens) works
with no phone. Protocol in slate-implementation-roadmap.md (§4).

## InfiniTime parity (low-level) vs Slate (high-level)
**Mirror InfiniTime** for boot, MCUBoot/DFU, flash map, WDT/button reset, BLE radio
bring-up, and other sealed-watch recovery paths
(https://github.com/InfiniTimeOrg/InfiniTime). Prefer their proven behaviour over
Slate-invented alternatives when both solve the same hardware problem.

**Differ on purpose** only above that: SDP display lists, local UI tiles, companion
JS apps telling the watch what to draw, and phone-side bridge/host/client logic.

Concrete example: WDT reload is withheld while the side button is held (same as
InfiniTime `SystemTask`), and the FreeRTOS **tick/idle hooks do not pet at all** —
only the app task loop (plus bounded flash helpers, the renderer tile loop,
and future tickless `POST_SLEEP`). A wedged app starves the bootloader dog
within ~7 s.

The renderer was added to that list on 6 Aug: a sub-app display list drawing
large filled circles took the app task past the dog and reset the watch, and
no BLE-facing input may be able to do that. The pet is inside the bounded
30-iteration tile loop, so it extends the deadline by one render and no more,
and it runs through `pet_service()` — which withholds the reload while the
side button is held, so long-press recovery is unaffected. Residual risk: a
hypothetical loop that calls the renderer repeatedly would no longer
auto-reset. Nothing does this today.

## Conventions
- Drivers are C++ classes, no dynamic allocation after init.
- All tasks document their stack size; check high-water marks in debug builds.
- Anything BLE-facing gets a host-side unit test that runs on desktop.
- SDP framing (§4.2) lives in `sdp_frame.hpp` / `sdp_frame.cpp`; BLE link/GATT in
  `ble_*.hpp`. UUID base and mbuf math: `docs/ble.md`.

# Project: Slate companion — Android bridge, script host and app repository client

## What this app is
Three things: a BRIDGE (adapts notifications, media, navigation, health from other apps),
a HOST (runs downloaded JS sub-apps in a sandbox), and a CLIENT (pushes display lists to
the watch over BLE, receives input events).

## Sub-app rules — READ `docs/subapp-rules.md` FIRST

**Before writing, reviewing, accepting or debugging a JS sub-app, read
`docs/subapp-rules.md`.** It is normative and it is short. It carries the
budgets that stop a sub-app resetting the watch (one already did), the required
comment header, and the settings schema.

Do not infer limits from the code; the numbers are in that document, and they
came from hardware.

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

## Mirroring InfiniTime is the DEFAULT for low-level code

The owner has stated this repeatedly: for hardware-level behaviour, mirror
InfiniTime unless there is a written reason not to. Reference tree:
`C:\Users\highj\Documents\Projects\InfiniTime-main`.

**Five defects in one session were divergences** — battery ADC (N-12), touch
read shape, touch controller config, touch IRQ trigger, and blocking
vibration. Each cost a build-and-flash cycle to find. Check InfiniTime FIRST
when touching a driver; do not debug outwards from the symptom.

Audit status and file pairs: `docs/infinitime-parity.md`.

The four axes where the defects actually were:
1. **Initialisation ordering** — what is configured, and in what order.
2. **Who initiates** — watch or phone, driver or caller.
3. **Worst-case duration inside a callback or ISR** — InfiniTime uses FreeRTOS
   timers where Slate has busy-waits.
4. **Who writes peripheral ENABLE registers**, and what that costs elsewhere.

## Working practice (added 5 Aug 2026)

- **`docs/issue-prompts-open.md` is the single point of truth** for open work
  and current state. `issues.md` is historical and partial; where they
  disagree, the prompts doc wins. Update it as work proceeds, not in a batch
  at the end.
- **The operator's observation outranks the agent's inference.** They can see
  the hardware; the agent cannot. If they report something is not on screen,
  that is the fact to work from — do not argue it from code reading.
- **Verify before claiming.** For the companion, `adb shell uiautomator dump`
  gives element bounds — presence in the view hierarchy is not visibility
  (`targetSdk 35` forces edge-to-edge, so content can render under the system
  bars). For the watch, ask; there is no way to see it otherwise.
- **Bump `versionCode` in `companion/app/build.gradle.kts` on every build that
  gets installed.** A static version makes the on-screen version useless.
- **Guard at the choke point.** `Core::show_current()` has twelve internal
  callers — a rule about when the local face may paint belongs inside it, not
  at each call site.
- **Instrument before theorising.** Diag line 3 exists because guessing at the
  SDP path cost several build-and-flash cycles. Add a counter, take one
  reading, then fix.
- **Every handover states what changed**, including incidental edits, since
  each flash costs the operator real time.
