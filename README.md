# SlateOS

**Open-source thin-client smartwatch firmware for the [PineTime](https://pine64.org/devices/pinetime/), plus an Android companion app and a JS sub-app ecosystem.**

<img src="/docs/images/face.jpg"
     alt="Watch face on PineTime"
     width="200">
<img src="/docs/images/settings.jpg"
     alt="Settings on PineTime"
     width="200">
<img src="/docs/images/apps.jpg"
     alt="App launcher on PineTime"
     width="200">
<img src="/docs/images/local map app.jpg"
     alt="Local Map app on PineTime"
     width="200">
	 
I love my PineTime. I wanted it to be able to do more! SlateOS, based on Pine's own InfiniTime (https://github.com/InfiniTimeOrg/InfiniTime) at the low level, is a departure from the traditional smartwatch OS, in that it literally turns your PineTime into a slate for your phone. In this way it also brings apps to your PineTime, without changing the firmware! 

How? well, we ususally install and run apps on our smartwatches, treating them as a full blown computing devices. But smartwatches conserve power by not having the beefiest CPUs and memories. And we usually have our phones nearby when we wear our smartwatches. So why not let the phones do all of the heavy lifting?

The Slate companion app acts as a bridge on your phone, running Javascript sub-apps, which render on your PineTime and which you can interact with on the watch. Many of will be able to (vibe)code a JS sub-app. I can't wait to see what you come up with!

Forgot your phone? No worry? SlateOS supports watch face, step counter and heart rate monitor in its offline mode.

Navigation: Swipe left for the app launcher, right for settings. I always found using settings on a smartwatch cumbersome, so the watch settings are available both on the watch and in the companion app.

The phone pushes display lists over Bluetooth; the watch renders them and returns element-level input. A local resilient core (face, steps, settings, sleep/wake, OTA) keeps working with no phone attached. The watch never executes code received over BLE — display lists are data only.

Notifications: On my to-do list for today or tomorrow.

**SlateOS would not have been possible without the hard work done by the people at Pine, InfiniTimeOrg (https://github.com/InfiniTimeOrg, https://github.com/geekbozu, https://github.com/mark9064, https://github.com/FintasticMan), Nordic and the FreeRTOS project! You have my gratitude!**

**IMPORTANT**:

If needed, holding the button reboots the PineTime. The InfiniTime bootloader is untouched and accessible by holding the button during boot untile the pine cone turns red. Also heed:

1. This is a work in progress! A lot is already supported and stable, but more is to come, including a prettier companion app. Gadgetbridge, linking to health apps etc. are currently not supported.
2. You need to know what side-loading on Android means, and what risks it carries.
3. You need to know what PineTime's green, blue and red pine cones mean.
4. If you want to develop your own sub-apps, then you will at least need to know how to vibe code effectively. If you know how to write and debug JS by hand, then all the better! Make sure to follow [`docs/subapp-rules.md`]
5. I am not responsible if you brick your device, or download someone's wonky JS sub-app ([`docs/subapp-rules.md`]!)

| | |
|---|---|
| **MCU** | Nordic nRF52832 (Cortex-M4F, 512 KB flash, 64 KB RAM) |
| **Firmware** | FreeRTOS + NimBLE + LittleFS + MCUBoot, C++17 |
| **Companion** | Kotlin, Jetpack Compose, raw `android.bluetooth`, JS sub-apps in V8 |
| **Protocol** | SDP (Slate Display Protocol) — see `slate-implementation-roadmap.md` §4 |

## What’s in the box

```
src/ include/     Watch firmware
companion/        Android app + SDP Kotlin core, emulator, tests
releases/         Prebuilt slate-dfu.zip + companion APK
docs/             Operator manuals, OTA, I2C rules, capabilities
shared/           Wire/font assets shared by codegen
scripts/          DFU packaging, tooling
bootloader/       MCUBoot / key notes for sealed installs
```

**Highlights (today)**

- Sealed install from InfiniTime via Nordic legacy DFU; later updates over SDP OTA
- Local face (time, steps, optional heart-rate BPM), scrollable settings, raise-to-wake
- Phone compositor + JS sub-apps (timer, map, navigation demos, …)
- Dual 240×8 tile renderer — no full framebuffer (RAM hard limit)

Capability map (keep this honest): [`docs/capabilities.md`](docs/capabilities.md).

## Quick start

1. Sideload [`releases/slate-companion-debug.apk`](releases/slate-companion-debug.apk) (or build the companion in Android Studio).
2. Put [`releases/slate-dfu.zip`](releases/slate-dfu.zip) on the phone.
3. On the PineTime, run InfiniTime (or recovery) and enable firmware updates.
4. In the Slate companion, open **Install Slate on sealed PineTime**, pick `slate-dfu.zip`, and flash.
5. Use **Associate watch (CDM)** and start the link service if needed. A new image shows an amber trial bar until IMAGE_OK is confirmed.
6. In the companion app, via the "Sub-app repository" button, you can select which sub-apps you want listed in SlateOS' launcher.

### Companion (Android)

```powershell
cd companion
.\gradlew.bat :app:installDebug
```

Details: [`companion/README.md`](companion/README.md). Associate the watch with Companion Device Manager, grant Bluetooth / notification access, then connect from the app.

### Firmware DFU zip (sealed PineTime)

```powershell
cmake -S . -B build/dfu -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DBOOTLOADER_PRESENT=ON
cmake --build build/dfu --target slate_dfu
Copy-Item -Force build/dfu/slate-dfu.zip releases/slate-dfu.zip
```

Output: `build/dfu/slate-dfu.zip` (and the tracked copy under `releases/`). Flash while InfiniTime (or recovery) is running — see [`docs/flash-sealed.md`](docs/flash-sealed.md). Once Slate is running, prefer in-app **Update Slate firmware (SDP OTA)**.

### Host tests (no hardware)

```powershell
cmake -S . -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests -E ble_link
```

```powershell
cd companion
.\gradlew.bat :sdp-tests:test
```

## Documentation

| Doc | Topic |
|-----|--------|
| [`docs/operator-manual.md`](docs/operator-manual.md) | Using the watch day to day |
| [`docs/companion-manual.md`](docs/companion-manual.md) | Phone app |
| [`docs/flash-sealed.md`](docs/flash-sealed.md) | First install on a sealed PineTime |
| [`docs/ota.md`](docs/ota.md) | SDP OTA and flash map |
| [`docs/i2c-bus.md`](docs/i2c-bus.md) | Shared I2C rules (touch / accel / HR) |
| [`docs/subapp-rules.md`](docs/subapp-rules.md) | JS sub-app budgets and policy |
| [`docs/ble.md`](docs/ble.md) | GATT / mbuf notes |
| [`CLAUDE.md`](CLAUDE.md) | Project conventions for contributors and agents |

Roadmap / protocol: [`slate-implementation-roadmap.md`](slate-implementation-roadmap.md).

## Architecture (short)

```text
┌─────────────┐   SDP over BLE    ┌──────────────────┐
│  Companion  │ ◄───────────────► │  PineTime/Slate  │
│  (Android)  │  display lists    │  tile renderer   │
│  JS host    │  input events     │  local face/core │
└─────────────┘                   └──────────────────┘
```

Low-level bring-up and recovery lean on [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime) patterns (MCUBoot map, WDT, radio). High-level UI and apps are Slate’s own thin-client model.

## Status / contributing

This is an active research and product bring-up tree. Expect sharp edges; prefer the docs above and [`docs/issue-prompts-open.md`](docs/issue-prompts-open.md) over guessing.

Hardware pinout and hard constraints are authoritative in [`CLAUDE.md`](CLAUDE.md) — do not invent alternate SPI/I2C wiring.

## License

Copyright (C) 2026 Daniel Hugelmann. SlateOS is released under the [GNU General Public License v3](LICENSE) (or later). Low-level bring-up follows [InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime) (also GPLv3). FreeRTOS is MIT; other vendored code under `third_party/` keeps its own licenses.
