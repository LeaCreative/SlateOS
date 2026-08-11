# Slate companion — what every control does

**Companion `0.8.2-p38` (build 39).** Updated 10 August 2026.

> Capability overview: [`capabilities.md`](capabilities.md). This page is the
> main-screen control map. Sub-apps are launched from the **watch launcher**,
> not from per-app buttons on the phone (those were removed).

---

## 1. Setup — do these in order, once

| Button | What it does |
|---|---|
| **1. Grant permissions** | Runtime BLE / notification permissions |
| **1c. Grant sub-app permissions (location, camera)** | Runtime grants for JS apps that need them |
| **1b. Notification access (system settings)** | Deep-link for `NotificationListenerService` (cannot be granted in-app) |
| **2. Associate watch (CDM)** | CompanionDeviceManager `watch` profile |
| **3. Start / reconnect link service** | Starts `LinkForegroundService` (owns GATT) |

---

## 2. Screens the phone can push

| Control | What it does |
|---|---|
| **Open TestApp** | Kotlin transport probe (black + white touch rect) |
| **Open Notifications** | Debug-only Kotlin list (primary UX is the watch shade) |
| **Watch launcher (on watch)** | Swipe face right→left; tap a JS app; swipe left→right to close |
| **Bundled JS apps** | timer, navigation, camera, vibrate, location, map (seeded into repo) |

Focus stack empty → watch **local face**. New notifications no longer steal
focus via INTERRUPT; swipe **down** on the face for the local shade.

---

## 3. Notifications

| Button | What it does |
|---|---|
| **1b. Notification access** | Required for the SYSTEM stub bridge |
| **Grant phone state** (system) | Optional `READ_PHONE_STATE` for real incoming-call alerts |

On arrival the companion pushes a **stub** (app name only). The watch vibes
twice and shows a status glyph. Tap a row to fetch the body. Reading on the
watch clears the stub there only. See [`notifications.md`](notifications.md).

---

## 4. Firmware and diagnostics

| Button | What it does |
|---|---|
| **Install Slate on sealed PineTime** | Nordic legacy DFU — InfiniTime → Slate first hop |
| **Update Slate firmware (SDP OTA)** | Channel-5 OTA, Slate→Slate (primary update path) |
| **Ping RTT (DIAG ch7)** | Round-trip probe |
| **Benchmarks (gates A / B / D)** | Performance gates |
| **Troubleshooting (BLE one-slot)** | Contention when another central holds the link |
| **View log** | In-app log (Pause/Copy/Share/Clear/Tail) |
| **Disconnect** | Drop GATT |

**Open with / Share a `.zip`:** activity label **Open with Slate**.
Sub-app zip → sideload install. `slate-dfu.zip` → SDP OTA screen with package
pre-selected (tap Start). Sealed first-hop is **not** routed via Open-with.

---

## 5. Scripting and platform

| Button | What it does |
|---|---|
| **Watch settings** | Raise / timeout / show-steps; syncs with the watch |
| **Script console** | Evaluate JS against the sub-app runtime |
| **Sub-app repository** | Browse/install JS apps, launcher visibility, per-app settings |
| **Background reliability settings** | OEM battery optimiser guidance |
| **Slate app battery / autostart settings** | Deep-link to this app’s OEM page |

---

## 6. Adapters (quick)

Wired for JS: UI, store, timer, haptic, phone vibrate, location, map, nav,
camera. **HTTP** is a stub. **Media** and **health** have permissions but no
host adapter yet. Details: `docs/script-runtime.md`, `docs/capabilities.md`.
