# Scripted sub-app runtime (M12)

Sub-apps are **downloaded JavaScript bundles**, never dex/JAR/.so. Play permits
interpreted code loaded at runtime; this host never blurs that line.

> **Writing or reviewing a sub-app? Read `docs/subapp-rules.md` first.** This
> document covers how the runtime works; that one is normative and covers what a
> sub-app is allowed to do — render budgets, the required comment header, and
> the settings schema. It exists because a sub-app reset the watch on 6 Aug.

## Isolate lifecycle

**One `JavaScriptIsolate` (or Rhino context) per running sub-app.**

Why not a shared isolate?

- §6.5 heap is **4 MB per isolate** — sharing makes accounting and kill/reclaim fuzzy
- A hostile script must not leave mutable V8 state for the next app
- Closing the isolate is the governor’s hard kill after overruns

`JsSlateAppEndpoint` + `ScriptEngine` keep the binding surface process-boundary
safe (JSON in / JSON out, base64 display lists) so Android IPC and desktop Rhino
share one contract.

## Engine choice

| Surface | Engine |
|---|---|
| Android companion | `androidx.javascriptengine` — V8 in an **isolated process** exclusive to the app |
| Desktop emulator + JVM tests | Mozilla Rhino in-process (same `slate.ui` / `__slate_dispatch`) |

On Android startup the host measures an empty `render()` round-trip through the
isolate IPC and writes it to the script console as `ipc`.

- If **≤ 20 ms**: keep javascriptengine (default).
- If **> 20 ms**: the console warns and we recommend **QuickJS in-process** as a
  fallback, still using `BindingSurface` only (no reflection, no filesystem, no
  raw Android objects). The `ScriptEngine` interface is the seam.

## Lifecycle (§6.3)

Exports (or globals) wired by `shared-js/slate_host.js`:

| Export | When |
|---|---|
| `onFocus` | Gained the screen (host also calls `render`) |
| `render` | Must be fast; returns base64 display list or outbound objects |
| `onInput` | TAP / BACK / … |
| `onEvent` | Adapter pushes (`timer`, notifications, …) |
| `onBlur` | Lost focus |

## Bindings (§6.3)

Whitelisted in `BindingSurface`. Effective permission = **manifest ∩ host-held
∩ source ceiling**.

The **Wired** column is the point of this table. Every row below has a
permission and a gate in `BindingSurface`, but only some have a host adapter
behind them — a binding can be declared, permission-checked and still do
nothing. `slate.location` sat in this table as though it existed for months
before it did.

| Binding | Permission | Wired | Notes |
|---|---|---|---|
| `slate.ui` / `invalidate` / `timer` / `haptic` / `log` | — | yes | Ungated. `timer` is still floored at 1000 ms by the Governor |
| `slate.store` | `storage` | yes | Also how declared settings reach a script (§5) |
| `slate.phone` | `vibrate` | yes | Buzzes the handset, not the watch motor. **No `requires` token exists for it** — `HostCapabilities.ALL` has no `slate.phone` |
| `slate.location` | `location` | yes (7 Aug) | `LocationAdapter`; fixes arrive as `onEvent('location', …)`. Third-party-blocked by default |
| `slate.map` | `location` | yes (7 Aug) | North-up OSM vector map. Host renders and pushes; the script never sees map data. Gated on `location` because that is what it is built from |
| `slate.nav` | `navigation` | yes | Bound to one app id host-side |
| `slate.camera` | `camera` | yes | Bound to one app id host-side |
| `slate.http` | `http` (+ `allowedHosts`) | **stub** | `handleHttp` records intent and checks the allowlist; performs no I/O |
| `slate.notifications` | `notifications` | partial | Host handles `action`; no read binding |
| `slate.media` | `media` | no | Permission and gate only |
| `slate.health` | `health.read` | no | Permission and gate only |

### Location (`slate.location`)

```js
slate.location.subscribe({ minIntervalMs: 5000, minDistanceM: 0 })
slate.location.request()      // one fix, from cache when it is < 2 min old
slate.location.unsubscribe()
```

Answers arrive as `onEvent('location', json)`, never as a return value —
getting a fix can take seconds and nothing on the compositor path may block:

```json
{ "type": "fix", "lat": …, "lon": …, "accuracyM": …, "altitudeM": …,
  "speedMps": …, "bearingDeg": …, "timeEpochSec": …, "provider": "gps" }
{ "type": "status", "state": "searching" | "denied" | "disabled" | "unavailable" }
```

`denied`, `disabled` and `unavailable` are **terminal** — no fix is ever
coming and the script cannot fix it, only the user can. A sub-app that treats
them as "not yet" will sit on a spinner forever. `examples/location` shows the
handling.

Host-side rules, all enforced in `LocationAdapter` rather than trusted to the
JS binding, since a script is untrusted input:

- Interval floored at **1000 ms**. A script cannot hold the GPS at any rate it
  likes.
- Coordinates rounded to **6 decimals** (~0.1 m) before leaving the phone.
- **One subscriber at a time.** A second app subscribing displaces the first,
  and the first is told `unavailable` rather than simply going quiet.
- The subscription is dropped on link loss and on service stop, so a dropped
  connection cannot leave the GPS running for a screen that no longer exists.

### Map (`slate.map`)

```js
slate.map.subscribe({ radiusM: 400 })
slate.map.unsubscribe()
```

Status arrives as `onEvent('map', json)`; the **map itself never does**. The
host renders the display list and pushes it under the app's focus, exactly as
the camera preview does. A sub-app using this must declare
`"refreshPolicy": "manual"` and draw nothing while a map is up, or it paints
over it.

```json
{ "type": "status", "state": "ok", "ways": …, "dropped": …, "bytes": …,
  "scaleM": …, "ageSec": … }
{ "type": "status", "state": "locating" | "loading" | "waiting" }
{ "type": "status", "state": "blocked", "detail": "denied" | "disabled" }
{ "type": "status", "state": "error", "detail": "…" }
```

**There is deliberately no refresh command.** The companion decides when to
redraw and when to refetch, so a downloaded script cannot poll a free public
API and cannot get the cadence wrong. That is a design constraint, not an
omission — do not add one.

Backgrounding matters here: fixes only keep arriving with the app in the
background if `LinkForegroundService` claimed the `location` foreground-service
type, which it does **only when the runtime permission was already granted at
service start**. A grant that arrives later needs
`LinkForegroundService.refreshForegroundServiceType()`, which the
"1c. Grant location" button calls. Claiming the type unconditionally would
throw `SecurityException` on Android 14+ and take the whole link service down
with it.

## Governor (§6.5)

| Resource | Limit |
|---|---|
| `render` / focus | 50 ms then kill call |
| `onInput` / `onEvent` | 200 ms |
| Heap | 4 MB / isolate |
| Timers | ≥ 1000 ms |
| Storage | 256 KB / app |
| Repeated violations | disable sub-app + console diagnostic |

## Display-list parity

`companion/shared-js/slate_ui.js` must emit **byte-identical** SDP to the Kotlin
DSL. Enforced by:

```powershell
cd companion
.\gradlew.bat :sdp-tests:test --tests "slate.script.JsUiGoldenTest"
```

## Sample: Timer

Source: `companion/examples/timer/` (copied into `sdp-core` resources).

Exercises `render`, tap input, `slate.timer` (≥1 s), and `slate.store` persistence.

On device: start the link service → **Open Timer (JS sub-app)** → **Script console**.

## Develop with NO watch and NO phone

1. Edit `examples/timer/main.js` and/or `shared-js/slate_ui.js`.
2. Run the desktop emulator:

```powershell
cd companion
.\gradlew.bat :sdp-emulator:run
```

3. Click **Load / reload timer**, tap Start/Stop on the 240×240 canvas.
4. Keep goldens green:

```powershell
.\gradlew.bat :sdp-tests:test --tests "slate.script.*"
```

Rhino is for desktop/CI only. Ship path on phone remains javascriptengine isolates.
