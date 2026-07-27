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
