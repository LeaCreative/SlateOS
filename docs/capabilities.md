# Slate — current capabilities

> **What the software can do today** (code in tree + hardware confirmation where
> noted). Roadmap wishes and closed defects live elsewhere; this file is the
> capability map so agents and operators do not under- or over-claim.
>
> Written **10 August 2026**. Prefer this over stale sections of
> `companion-manual.md` / older handover text. Open bugs and next work remain in
> [`issue-prompts-open.md`](issue-prompts-open.md).

---

## Versions (this tree)

| Piece | Identity |
|---|---|
| Accuracy after ACC_CONF `0x28` | **Acceptable** (operator, 10 Aug) |
| Last packaged DFU | `build/dfu/slate-dfu.zip` — SHA-256 prefix **`9421B271FDC3`** (stamp ~14:38); ACC_CONF `0x28`, ScreenBlock 256 B, swipe routing |
| Companion APK | **`slate.app.debug`** — see `companion/app/build.gradle.kts` for versionName / versionCode |
| Host tests | Run with `-E ble_link` (`ble_link` still fails on purpose until investigated) |
| Link RAM slack | **~3016 B** (`__StackLimit` − `__heap_end__`); static RAM ~89% after I-19 ScreenBlock reclaim |

---

## Firmware (watch)

### Thin client / BLE / SDP
- NimBLE: Slate GATT + Nordic legacy DFU GATT; BAS/DIS; no CTS (time via CONTROL TIME_SYNC).
- SDP channels 0–5 + 7: control, display lists, input, assets, system notifs, OTA, diag.
- Session negotiate, heartbeats, remote depth, CREDIT window (credit size is still a **placeholder**: 4096-or-0).
- Link → single-slot `AppInbox` → **app task** drain (not on NimBLE host).
- Validated display-list parse/interpret; dual 240×8 tile renderer (no full framebuffer).
- Input: tap / swipe / button → phone when remote owns the panel.

### Local resilient core
- **Face:** time, date, optional steps, battery, link cue, trial amber bar, version/diag overlay.
- **Settings** (swipe face **left→right**): raise-to-wake, display timeout (incl. never), show steps. Bidirectional `SETTINGS_SYNC` (0x21) with phone; persisted (`SLTU`).
- **Swipe policy:** settings **right→left** → face (not launcher); launcher **left→right** → close (not settings); face **right→left** → launcher when linked.
- **Notifs screen** (button cycle): local store from SYSTEM ch4 — numeric/monogram UI (no full fonts).
- Display sleep (default **20 s**, configurable); wake on button, double-tap, **raise-to-wake**, charger edge, alerts.
- Offline launcher swipe → “not connected”.
- Haptics via FreeRTOS timer motor driver.

### Sensors / power
- BMA421/425: Bosch feature upload, pedometer enable (full-block R/W), ACC_CONF **`0x28`** (InfiniTime CIC_AVG), axis swap for mount frame.
- **Steps** and **raise-to-wake** confirmed on hardware (N-59/N-60). False-wake on typing / arms-down walk: none observed.
- Battery SAADC InfiniTime-matched curve + charge pin; BAS.
- HRS3300 **gated by settings** — `hr_enabled` (default Off). On-demand measure
  from local Heart Rate screen or GATT HRS CCCD; asleep otherwise.
- WDT pet from app loop (+ renderer tile loop); tickless idle **off**.

### OTA / flash
- Flash map = InfiniTime MCUBoot contract; IMAGE_OK after ~10 s connected.
- **SDP channel-5 OTA** (Slate→Slate) — hardware-proven.
- **Nordic legacy DFU** into secondary — sealed first hop from InfiniTime.
- LittleFS on external NOR; asset channel can stage files — **renderer does not yet load real IMAGE/ICON packs** (opcodes draw placeholders; caps at 0).

### Task topology
- FreeRTOS: **app** + NimBLE **ll**/ **ble** + timer daemon (+ smoke self-delete). Roadmap multi-task split **deferred**.

### Not product-ready / deferred on watch
- Heart rate; tickless / deep sleep (I-3); real bitmap assets; alarm/timer **UI/protocol** (sched + alert draw exist, no production setters); true `free_dl_bytes`; multi-task topology.

---

## Companion (phone)

### Link / host
- CDM association, presence service, FGS `connectedDevice` (+ location type when granted).
- GATT session, compositor (focus, credit, push quota), in-app log, contention / troubleshooting UI.
- Empty focus stack → watch **local face** (Kotlin `ClockApp` is not forced as ambient base — N-34 mitigated this way).
- Watch settings UI + sync; confirm banner for trial firmware.

### Sub-apps
- **JS** via androidx.javascriptengine (V8 isolate); Kotlin DSL / JS builder byte-identical lists.
- **Watch launcher** (`LauncherApp`): swipe face→launcher, tap to focus, swipe back to close.
- **Bundled demos** (seeded): `timer`, `navigation`, `camera`, `vibrate`, `location`, `map`.
- **Sideload-only examples:** `image`, `image-vector` (zip).
- Repo UI + third-party sources; official hosted index still **placeholder**.
- **Open with / Share zip** (`SideloadActivity`, label “Open with Slate”):
  - Sub-app shape → install as third-party sideload.
  - DFU shape (`manifest.application` or `slate-mcuboot-image.bin`) → **SDP OTA** screen with URI pre-selected (Start still manual).
  - Sealed InfiniTime install remains the separate main-screen button (not open-with).

### Adapters (host-wired)

| Capability | Status |
|---|---|
| Notifications (NLS → ch4 store + Kotlin INTERRUPT app) | Working (JS notif app not yet) |
| Navigation (OsmAnd-style + demo) | Working |
| Camera (CameraX → RGB332 PATCH) | Working |
| Location | Working |
| Map (Overpass + host vector render) | Working |
| Haptic (watch) / phone vibrate | Working |
| HTTP | Stub (allowlist + log) |
| Media / Health | Permission gates only — **no adapter** |

### OTA UI
- **Install Slate on sealed PineTime** — Nordic DFU first hop.
- **Update Slate firmware (SDP OTA)** — channel 5; same `slate-dfu.zip` as sealed package.
- Open-with DFU → SDP OTA path only (see above).

---

## Operator gestures (watch)

| Gesture | Effect |
|---|---|
| Face, swipe L→R | Local settings |
| Settings, swipe R→L | Face |
| Face, swipe R→L (linked) | Phone launcher |
| Launcher, swipe L→R | Close to face |
| Side button | Cycle Face → Notifs → Settings (and related local handling) |
| Raise wrist (when enabled) | Wake display |
| Double-tap / button | Wake when asleep |

---

## Where to read next

| Need | Doc |
|---|---|
| Open bugs / next work | `docs/issue-prompts-open.md` |
| Agent handover / traps | `docs/handover-prompt.md` |
| Sub-app limits | `docs/subapp-rules.md` |
| Script bindings detail | `docs/script-runtime.md` |
| OTA / sealed flash | `docs/ota.md`, `docs/flash-sealed.md` |
| InfiniTime divergences | `docs/infinitime-parity.md` |
| Standing hardware rules | `CLAUDE.md` |
