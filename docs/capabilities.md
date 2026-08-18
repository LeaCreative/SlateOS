# Slate — current capabilities

> **What the software can do today** (code in tree + hardware confirmation where
> noted). Roadmap wishes and closed defects live elsewhere; this file is the
> capability map so agents and operators do not under- or over-claim.
>
> Written **18 August 2026**. Prefer this over stale sections of
> `companion-manual.md` / older handover text. Open bugs and next work remain in
> [`issue-prompts-open.md`](issue-prompts-open.md).

---

## Versions (this tree)

| Piece | Identity |
|---|---|
| Accuracy after ACC_CONF `0x28` | **Acceptable** (operator, 10 Aug) |
| Last packaged DFU | `build/dfu/slate-dfu.zip` — SHA-256 prefix **`EDAC341E7A03`** (`0.1.0-m21 Aug 18 15:56`, MCUBoot 0.1.21). **On wrist** after SDP OTA. |
| Companion APK | **`0.8.2-p84`** (versionCode **85**, `slate.app.debug`) — force reconnect + MAC scan then autoConnect fallback |
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
- **Face:** time, date, optional steps, battery, link cue, trial amber bar, version/diag overlay (all of Face diag, including the version line, hide together).
- **Settings** (swipe face **left→right**): raise-to-wake + Soft/Normal/Hard sensitivity, shake-to-wake (default Off) + sensitivity, display timeout (incl. never), show steps, face diag, HR. Bidirectional `SETTINGS_SYNC` v4 (0x21) with phone; persisted (`SLTY`).
- **Swipe policy:** settings **right→left** → face (not launcher); launcher **left→right** → close (not settings); face **right→left** → launcher when linked.
- **Notifs shade** (swipe down from face): SYSTEM ch4 stubs (notification titles); body on demand via INPUT `NOTIF_REQ` + SYSTEM `BODY`. Amber status glyph while stubs remain. Live stubs wake + DOUBLE haptic; **reconnect bulk sync and NLS resync use FLAG_SILENT** (no wake/haptic — needs matching firmware). Companion also skips unchanged NLS re-ingests and debounces NLS reconnect storms (`0.8.2-p65`).
- Display sleep (default **20 s**, configurable); wake on button, double-tap, **raise-to-wake**, optional **shake-to-wake**, charger edge, alerts.
- Offline launcher swipe → “not connected”.
- Haptics via FreeRTOS timer motor driver.

### Sensors / power
- BMA421/425: Bosch feature upload, pedometer enable (full-block R/W), ACC_CONF **`0x28`** (InfiniTime CIC_AVG), axis swap for mount frame.
- **Steps** and **raise-to-wake** confirmed on hardware (N-59/N-60), then **both raise and shake died** on the m17 image (N-61: ACC_DATA zeros / `PWR_CONF` cleared `fifo_self_wakeup`). **m18** restores InfiniTime `PWR_CONF` and re-enables acc on dead samples (packaged **`9FA5215458E0`**). **m19** adds Face-diag gating for the version line. The zip previously labelled m19 (**`2D7C272691C8`**) was actually **m18 Aug 17 17:59** (packaged after bumping docs, before rebuilding). Current package **`275FAE146A95`** is the real m19 (`0.1.0-m19 Aug 18 14:59`). Confirm on wrist after SDP OTA. Overlay line 1 is `uptime/chip.status.step.pwr` (`pwr=4` = acc_en).
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
- CDM association, presence service, FGS `connectedDevice` (+ location type when granted). **CDM `onDeviceDisappeared` ignored while GATT is up** (`0.8.2-p73+`); **FGS stays up when GATT is down** (`0.8.2-p82`) so the reconnect watchdog can `connectGatt` without a tap. Main / boot / APK replace start the FGS for the remembered watch. **Start / reconnect** force-aborts a hung `connectGatt`; connect path is MAC-filtered LE scan then `autoConnect` fallback (`0.8.2-p84`).
- GATT session, compositor (focus, credit, push quota), in-app log, contention / troubleshooting UI.
- Empty focus stack → watch **local face** (Kotlin `ClockApp` is not forced as ambient base — N-34 mitigated this way).
- **Watch settings UI + sync** (Face diag hides version + counter lines from **m19**); confirm banner for trial firmware.

### Sub-apps
- **JS** via androidx.javascriptengine (V8 isolate); Kotlin DSL / JS builder byte-identical lists.
- **Watch launcher** (`LauncherApp`): swipe face→launcher, tap to focus, swipe back to close.
- **Bundled demos** (seeded): `timer`, `navigation` (OsmAnd AIDL + dest remaining + destination reached), `camera`, `vibrate`, `location`, `map`, `news`, `media`, `weather`, `httpdemo`.
- **Sideload-only examples:** `image`, `image-vector` (zip).
- Repo UI + third-party sources; official hosted index still **placeholder**.
- **Open with / Share zip** (`SideloadActivity`, label “Open with Slate”):
  - Sub-app shape → install as third-party sideload.
  - DFU shape (`manifest.application` or `slate-mcuboot-image.bin`) → **SDP OTA** screen with URI pre-selected (Start still manual).
  - Sealed InfiniTime install remains the separate main-screen button (not open-with).

### Adapters (host-wired)

| Capability | Status |
|---|---|
| Notifications (NLS → stub store + local shade; body on tap) | Working |
| Incoming call alert (Telephony / call-notif fallback) | Working (display-only) |
| Navigation (OsmAnd AIDL + NLS + voice `reached_destination`; destination reached face) | Working (`slate.navigation` **1.2.2**) |
| Camera (CameraX → RGB332 PATCH) | Working |
| Location | Working |
| Map (Overpass + host vector render) | Working |
| Haptic (watch) / phone vibrate | Working |
| HTTP (`slate.http`) | Working — GET/POST, allowlist, 64 KiB cap |
| News (`slate.news`) | Host RSS/Atom adapter; JS UI |
| Media (`slate.media`) | Now-playing + transport (NLS) |
| Weather (`slate.weather`) | Open-Meteo snapshot; JS UI |
| Calendar | Working (p66) — CalendarContract next events |
| Alarms | Working (p66) — Clock intent / exact AlarmManager |
| Home | Working (p66) — Home Assistant REST |
| Health | Working (p76) — companion 24 h BPM vs time of day; Health Connect read/write of watch steps/BPM via VITALS |

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
| Face, swipe down | Notification shade |
| Notifs / detail, swipe up | Face |
| Face, swipe R→L (linked) | Phone launcher |
| Launcher, swipe L→R | Close to face |
| Side button | Return to Face from local screens (does not open Notifs) |
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
