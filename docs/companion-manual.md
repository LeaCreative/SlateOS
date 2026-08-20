# Slate companion — what every control does

**Companion `0.8.2-p85` (build 86).** Updated 18 August 2026.

> Capability overview: [`capabilities.md`](capabilities.md). Sub-apps launch from
> the **watch launcher**, not from per-app phone buttons.

Title bar **Light** / **Dark** sets the phone app theme (persisted) on Main,
Permissions, Debug, Watch settings, and the repository.

---

## Main

| Control | What it does |
|---|---|
| **Permissions** | Grants and OEM background settings |
| **Associate watch (CDM)** | CompanionDeviceManager `watch` profile |
| **Start / reconnect link service** | Starts `LinkForegroundService` (owns GATT). Aborts a hung `connectGatt`, MAC-scans, then connects; does not drop a live session |
| **Heart rate** | Rolling 24 h BPM graph vs time of day (from watch VITALS; HR must be On) |
| **Watch settings** | Raise / timeout / steps / face diag (incl. version line on watch) / HR / colours; syncs with watch |
| **Sub-app repository** | Browse/install JS apps, launcher visibility, per-app settings |
| **Install Slate on sealed PineTime** | Nordic legacy DFU — InfiniTime → Slate first hop |
| **Update Slate firmware (SDP OTA)** | Channel-5 OTA, Slate→Slate |
| **Debug** | Bring-up / diagnostics |
| **Disconnect** | Drop GATT |

Link metrics (MTU, PHY, errors) and the trial-image confirm banner stay on this
screen. Link RTT is measured under **Debug → Benchmarks** gate B (not a
one-shot on Main).

GATT **connected** is not session **Ready**. The watch launcher only paints
after `HELLO_OFFER` (log `CONTROL op=1`). Opening Main / tapping reconnect
starts a MAC-filtered LE scan then `connectGatt(false)` (`0.8.2-p84`); boot
uses `autoConnect`. If HELLO never arrives, the host rewrites TX CCCD 0→1
(`0.8.2-p85`). A swipe while still `Connected` is deferred and flushed on Ready.

**Open with / Share a `.zip`:** Sub-app zip → sideload. `slate-dfu.zip` → SDP OTA.
Sealed first-hop is **not** routed via Open-with.

---

## Permissions

| Control | What it does |
|---|---|
| **Grant BLE / notification permissions** | Runtime BLE + `POST_NOTIFICATIONS` |
| **Grant sub-app permissions (location, camera)** | For JS apps; refreshes FGS type after location grant |
| **Notification access (system settings)** | Deep-link for `NotificationListenerService` |
| **Background reliability settings** | Battery optimisation allowlist |
| **Slate app battery / autostart settings** | This app’s OEM details page |

Optional `READ_PHONE_STATE` for real incoming-call alerts is still granted via
system settings (no dedicated button).

---

## Debug

| Control | What it does |
|---|---|
| **Open TestApp** | Kotlin transport probe (not in the JS launcher) |
| **Open Notifications** | Kotlin list focus (primary UX is the watch shade) |
| **Script console** | Live JS runtime log (`slate.log`, timings, quota/governor) — not a REPL |
| **Troubleshooting (BLE one-slot)** | Contention when another central holds the link |
| **View log** | In-app link log (Pause/Copy/Share/Clear/Tail) |
| **Benchmarks (gates A / B / D)** | A throughput, **B RTT**, D render — needs debug firmware |

---

## Removed

| Former control | Why |
|---|---|
| **Ping RTT (DIAG ch7)** | Folded into **Benchmarks** gate B |
| **Interrupt filter** buttons + `NotifPrefs` | Notifications no longer steal focus via INTERRUPT |

---

## Adapters (quick)

Wired for JS: UI, store, timer, haptic, phone vibrate, location, map, nav,
camera, news (host RSS), media (NLS), weather, calendar, alarms, home, health,
HTTP. Details: `docs/script-runtime.md`, `docs/flagship-apps.md` (nav/camera),
`docs/capabilities.md`.
