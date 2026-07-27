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

1. Grant Bluetooth / notification permissions  
2. **Associate watch (CDM)** — `DEVICE_PROFILE_WATCH` on API 33+  
3. Link FGS starts (`foregroundServiceType=connectedDevice`), connects GATT, requests MTU 247 / 2M PHY  
4. Pushes a clock display list once per second (M4 DSL + §4.2 framing on channel 1)

On-screen readout: ATT MTU, PHY, connection interval (from STATUS), RTT (DIAG ch7 loopback).

Logcat: `adb logcat -s SlateLink`

**Reality check:** a live push needs Slate firmware with M5 BLE on the watch. Until DFU exists, association may find nothing / GATT won't see the Slate service — the phone stack is still complete.

## Layout

| Module | Role |
|---|---|
| `sdp-core` | DSL, framing encoder, UUIDs |
| `sdp-emulator` | Desktop Compose UI preview |
| `sdp-tests` | Golden + framing tests |
| `app` | Android CDM + FGS + raw GATT client |
