# Flagship sub-apps (M16) — Navigation & Camera

Both apps are **downloaded JavaScript** packages (`slate.navigation`, `slate.camera`)
installed into the M13 `InstalledStore` (same path as the repository client). They are
**not** compiled-in Kotlin `SlateAppEndpoint`s. On first link-service start,
`BundledPackageSeeder` writes official demo `.slate` contents into the store so the
apps are runnable before a hosted index exists.

## A) Navigation

### Adapter

`NavAdapter` is a generic maneuver source:

| Action | Role |
|---|---|
| OsmAnd AIDL `registerForNavigationUpdates` | Turn type + metres to next turn (travel-relative) |
| OsmAnd navigation notification (NLS) | Remaining metres to destination + street text |
| `slate.app.NAV_MANEUVER` | Slate demo / CI |
| `net.osmand.navigation` / `net.osmand.plus.NAVIGATION` | OsmAnd-style extras |

Extras mapped: `turn` / `turn_type` / `maneuver`, `distanceM` / `distance`, `street` /
`streetName`, `progressPct`, `etaEpochSec` / `eta`, `destinationDistanceM` /
`time_distance_left`, `status`.

JSON to script: `turn`, `distanceM`, `destinationDistanceM`, `street`, `status`, …

Script API: `slate.nav.subscribe()` / `unsubscribe()` / `demo(kind)`. Maneuvers arrive as
`onEvent('nav', json)` with `type=maneuver`. Turn arrows are relative to **direction of
travel**, not phone compass orientation.

### Screen

Maneuver glyph, distance, street, `progressArc`, ETA. Manifest `refreshPolicy: on-change`.
The script pushes **one display list per maneuver event** (plus `retain(120)`), never a
periodic refresh.

### Mid-route failure behaviour

| Condition | Behaviour |
|---|---|
| **GPS lost** | Host posts `status=lost_gps` (keep last turn/distance). UI shows “GPS lost”; no haptic spam. |
| **Phone ↔ watch disconnect** | Host calls `NavAdapter.notifyDisconnected()` → `status=disconnected`. Script would push a DL if the link were up; **watch keeps the last retained frame** (`RETAIN`). On reconnect, the next maneuver event refreshes. |
| **Demo** | Long-press / double-tap → `slate.nav.demo('left')`. Also `demo('lost_gps')` / `demo('disconnected')`. |

## B) Camera — binding design

### Verdict: privileged host + thin script surface

**Preview pixels must not enter the JS isolate.** A 60×60 RGB332 frame is 3.6 KB at ~10 fps —
routing that through V8/Rhino IPC would burn Gate F budget, enlarge the attack surface, and
fight Play’s “interpreted only” line if anyone later “helped” by passing bitmaps into script.

| Layer | Owns |
|---|---|
| **Script (`slate.camera`)** | `start` / `stop` / `capture`; chrome UI; shutter tap; status events |
| **Host (privileged)** | CameraX analysis, downscale, RGB332 convert, `FrameGovernor` (drop, don’t queue), PATCH display lists via `Compositor.pushHostDisplayList` |

Third-party packages declaring `camera` are blocked by default (`PermissionPolicy`); only
official (or user-granted) apps get the binding.

If product needs a richer viewer later, keep a **built-in** streaming path and leave the
script API as a controller — do not move frame buffers into the sandbox.

### Pipeline

1. CameraX `ImageAnalysis` (KEEP_ONLY_LATEST) or synthetic pattern if bind fails.
2. Phone-side → 60×60 RGB332.
3. `FrameGovernor` (~36 kB/s budget, max 1 in-flight) **drops** frames when over budget.
4. Host pushes PATCH-only lists (no CLEAR) so script chrome stays.

## Battery vs §8 (~6% / 30 min)

§8: active session ~20–25 mA → **~11 mAh / 30 min ≈ 6% of 180 mAh**.

No PPK on a sealed housing (see `docs/power.md`). **Budget model**, not lab µA:

| Session | Radio profile | Est. watch current | Est. % / 30 min | Est. % / min | vs 6%/30min |
|---|---|---|---|---|---|
| **Navigation** (one DL / maneuver, ~1–4/min typical) | Active ~30–50 ms interval, mid backlight | **20–25 mA** (target active) | **~6%** | **~0.20%/min** | **At budget** if maneuver rate stays event-driven |
| **Navigation** if mistakenly periodic @1 Hz | Same + continuous ASSET traffic | 25–30 mA+ | **>6%** | higher | **Fails budget** — do not refresh periodically |
| **Camera** streaming PATCH @~10 fps, 15 ms interval | Streaming tier | **25–30 mA** | **~7–8%** | **~0.25–0.27%/min** | **Above nav budget** — hard time-box sessions |

**Per minute (model):** nav ≈ **0.20% / min**; camera ≈ **0.25–0.27% / min**.

Fill a Measured column when a battery-lead or charge-path probe exists. Gate C (active >30 mA hard-fail) still applies to both profiles.
