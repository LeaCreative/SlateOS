# Slate companion

Kotlin DSL, desktop emulator, golden tests, and the M6 Android link app.

## Prerequisites

- JDK 17+
- Android SDK (for `:app`) — `local.properties` with `sdk.dir=...`
- Run `python tools/codegen/generate.py` from the repo root after changing `shared/`.

## Host / emulator

```powershell
cd companion
.\gradlew.bat :sdp-tests:test
.\gradlew.bat :sdp-emulator:run
```

## Android app (M6)

```powershell
cd companion
.\gradlew.bat :app:assembleDebug
.\gradlew.bat :app:installDebug
```

APK: `app/build/outputs/apk/debug/app-debug.apk`

On the phone:

1. Grant Bluetooth / notification permissions (camera is requested only when used)  
2. **Associate watch (CDM)** — `DEVICE_PROFILE_WATCH` on API 31+ (needs
   `REQUEST_COMPANION_PROFILE_WATCH` in the manifest)  
3. Link FGS starts (`foregroundServiceType=connectedDevice`), connects GATT, requests MTU 247 / 2M PHY  
4. Pushes a clock display list once per second (M4 DSL + §4.2 framing on channel 1)

On-screen readout: ATT MTU, PHY, connection interval (from STATUS), RTT (DIAG ch7).

**Compositor (M8):** ambient `ClockApp` + `TestApp` via process-boundary-safe host contract —
see `docs/compositor.md`. Main screen → **Open TestApp**.

**Benchmarks:** main screen → **Benchmarks (gates A / B / D)** — see `docs/benchmark.md`.

Logcat: `adb logcat -s SlateLink`

**Sealed first install (InfiniTime → Slate):** choose **Install Slate on sealed PineTime**,
enable firmware updates in InfiniTime, associate the InfiniTime/recovery target, select
the application-only `slate-dfu.zip`, and install. The transfer uses raw `BluetoothGatt`
Nordic legacy DFU in a `connectedDevice` foreground service.
PineDFU/SoftDevice targets, non-Slate archives, non-PineTime device types, and
images without an MCUBoot header are rejected before transfer.
If the watch is already running Slate, the activity redirects to the SDP OTA screen.

**Post-install update (Slate → Slate SDP OTA):** with the watch already connected,
choose **Update Slate firmware (SDP OTA)**. Select the same `slate-dfu.zip`, tap
*Start OTA update*. The image is SHA-256 verified, streamed over SDP channel 5 with
credit flow-control, and committed with an automatic MCUBoot IMAGE_OK on the next
reconnect. The transfer is resumable across connection drops. See `docs/ota.md`.

## Layout

| Module | Role |
|---|---|
| `sdp-core` | DSL, framing encoder, UUIDs |
| `sdp-emulator` | Desktop Compose UI preview |
| `sdp-tests` | Golden + framing tests |
| `app` | Android CDM + FGS + raw GATT client |
