# Prebuilt packages

Tracked installers for people who want to try Slate without building.

| File | What it is |
|------|------------|
| [`slate-dfu.zip`](slate-dfu.zip) | InfiniTime-compatible DFU package (MCUBoot image) for a sealed PineTime |
| [`slate-companion-debug.apk`](slate-companion-debug.apk) | Debug companion (`slate.app.debug`) — sideload on Android |

Rebuild and refresh these when you cut a public drop:

```powershell
# Firmware
cmake --build build/dfu --target slate_dfu
Copy-Item -Force build/dfu/slate-dfu.zip releases/slate-dfu.zip

# Companion
cd companion
.\gradlew.bat :app:assembleDebug
Copy-Item -Force app/build/outputs/apk/debug/app-debug.apk ..\releases\slate-companion-debug.apk
```

Intermediate `build/` trees stay gitignored; only this folder is meant to ship binaries.
