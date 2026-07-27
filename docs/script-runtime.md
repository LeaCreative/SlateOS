# Scripted sub-app runtime (M12)

Sub-apps are **downloaded JavaScript bundles**, never dex/JAR/.so. Play permits
interpreted code loaded at runtime; this host never blurs that line.

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

Whitelisted in `BindingSurface`. Effective permission = **manifest ∩ host-held**.

| Binding | Permission |
|---|---|
| `slate.ui` / `invalidate` / `timer` / `haptic` / `log` | — |
| `slate.store` | `storage` |
| `slate.http` | `http` (+ `allowedHosts`) |
| `slate.notifications` | `notifications` |
| `slate.media` | `media` |
| `slate.location` | `location` |
| `slate.health` | `health.read` |

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
