# Sub-app rules and standards

**Read this before writing, reviewing or accepting a JS sub-app.** It is
normative: a sub-app that breaks a MUST here is a defect in the sub-app, and a
host that *lets* it break one is a defect in the host.

Every limit below has a number attached, and every number came from something
that actually happened on hardware. Nothing here is a guess.

---

## 1. Why this document exists

On 6 Aug 2026 a sub-app (`examples/image-vector`) **reset the watch** by asking
it to draw 23 filled circles, several of radius ~120. That was not a hostile
app; it was a reasonable piece of art. Three properties combined:

1. A filled circle was implemented as `rad + 1` concentric Bresenham outlines —
   ~58,000 plotted points for one r=120 disc.
2. The renderer replays the **entire display list once per tile**, and there are
   **30 tiles**. Every cost in a list is therefore multiplied by 30.
3. The renderer runs on the app task, which is the only thing that pets the
   watchdog. Several seconds of drawing starved the ~7 s bootloader dog.

The firmware has since been fixed on all three counts (span fills, watchdog pets
inside the tile loop). But the episode established the principle:

> **A sub-app is untrusted input. It must not be able to reset, wedge or
> silently blank the watch — and where it can, that is the firmware's bug.
> Sub-apps still have a duty not to try.**

---

## 2. Hard limits (MUST)

| Limit | Value | Enforced by | What happens if you exceed it |
|---|---|---|---|
| Display list size | **4096 B** (`kMaxListBytes`) | firmware parser | List rejected, `dl_rej` increments, screen unchanged |
| Practical list size | **≤ 2048 B** | credit window | See §3 — larger lists are dropped intermittently, not reliably |
| Coordinates | 0–239, `u8` | firmware parser | Op rejected, whole list rejected |
| Scroll content height | ≤ declared `contentH` | firmware parser | Whole list rejected |
| Font ids | 0 (3x5) or 1 (5x7) | firmware parser | Whole list rejected |
| Script eval deadline | **500 ms** per call | script host | Isolate killed, sub-app dies |
| Storage | 256 KB | binding surface | `store.set` denied, logged as quota |
| Elem nesting depth | 8 | firmware parser | List rejected |
| Hit elements | 32 | firmware parser | Extra elements get no tap target |

### 2.1 Rendering cost

The list is replayed **once per tile, 30 times**. Budget accordingly.

- **MUST NOT** rely on the renderer clipping expensive geometry cheaply. It does
  clip, but the op is still walked 30 times.
- **SHOULD** keep a full screen under ~150 drawing ops. The launcher (4 rows,
  ~16 ops) renders in well under 30 ms; `image-vector` (23 large filled discs)
  took seconds before the span-fill fix.
- **Filled circles** are now scanline spans, O(r) per disc rather than O(r²).
  They are affordable again, but a screen of 20+ large discs is still the most
  expensive thing you can draw.
- **Prefer** `rect`, `rectRound` and text. Prefer one large fill to many small
  ones.

### 2.2 No unbounded work in JS

- **MUST NOT** write an unbounded or data-dependent loop in `onFocus`,
  `render`, `onInput` or `onEvent`. The 500 ms eval deadline kills the isolate,
  and on WebView builds without per-isolate termination it takes the **whole
  sandbox** down — every other running sub-app with it.
- **MUST** bound any loop by a literal constant you can see in the source.
- **SHOULD** precompute static geometry into a `const` array at module scope
  (as `image-vector` does) rather than recomputing it per render.

### 2.3 Behaviour

- **MUST** return promptly from every entry point. These run on the phone's
  compositor path; slow handlers stall the watch's screen, not just yours.
- **MUST** handle `ev.op === 0x06` (BACK) by relinquishing focus, or the user
  can only leave via the side button.
- **SHOULD** treat `onFocus` as "build my screen from scratch" — it may be
  called repeatedly, including when the user re-opens the launcher.

---

## 3. The credit window (why a big screen fails *intermittently*)

The watch advertises its display buffer as free (4096) or busy (0) — it does not
report real byte counts yet. The phone runs a decrementing budget against that.
A screen only reaches the watch if it fits the window at that instant.

**Consequence: a 3987 B screen works sometimes and not others.** That is not
flakiness in your app; it is the flow-control model, and it is why §2 says keep
lists under ~2 KB even though the parser accepts 4 KB.

A dropped push is logged by the companion with its reason and the numbers:

```
pushToWatch DROPPED: slate.image, 3987 B — credit: need 3987 B, window has 109 B
```

If your screen does not appear, look for that line before assuming your script
is broken.

---

## 4. Required comment header

Every `main.js` **MUST** open with a header block covering, in this order:

1. **Name** and one-line purpose.
2. **What it draws** — the screen, in words.
3. **What it does** — inputs it handles, adapters it calls, side effects.
4. **Permissions** it declares and why each is needed.
5. **Budget notes** — anything unusual about size or cost.

The rule that "sub-apps are downloaded JavaScript, never dex/JAR/.so" is worth
restating in the header of any app that could be mistaken for otherwise.

```js
/**
 * Timer — countdown with start/stop, on the watch.
 *
 * Draws: MM:SS at scale 6, one full-width button below it.
 * Does:  tap toggles running; a 1 s host timer decrements; buzzes the watch
 *        motor at zero. Persists remaining/running across focus changes.
 * Perms: storage (remaining + running survive a blur).
 * Budget: ~60 B display list, well inside every limit.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
```

---

## 5. Settings (optional)

A sub-app **MAY** declare user-editable settings. If it does, the companion
shows a gear next to its entry in the repository list, and tapping it opens a
generated settings screen. No UI code goes in the sub-app.

### 5.1 Declaring

In `manifest.json`:

```json
"settings": [
  {
    "key": "durationSec",
    "label": "Countdown duration",
    "type": "int",
    "default": 60,
    "min": 5,
    "max": 3600,
    "unit": "s",
    "help": "How long the countdown runs from Start."
  },
  {
    "key": "feedUrl",
    "label": "RSS feed URL",
    "type": "string",
    "default": "",
    "max": 512,
    "help": "https URL of an RSS or Atom feed."
  }
]
```

| Field | Required | Notes |
|---|---|---|
| `key` | yes | Also the storage key the script reads |
| `label` | yes | Shown in the settings screen |
| `type` | yes | `int`, `bool`, `choice`, or `string` |
| `default` | yes | Used until the user changes it |
| `min` / `max` | int: range; string: `max` = max length (default 512) | Clamped by the host; the script may still validate |
| `options` | choice only | Array of `{ "value": …, "label": … }` |
| `unit` | no | Suffix shown after the value |
| `help` | no | One line under the control |

### 5.2 Reading

Settings are **seeded into the sub-app's store**, so there is no new binding to
learn:

```js
var seconds = parseInt(slate.store.get('durationSec'), 10);
if (isNaN(seconds)) seconds = 60;   // MUST still default defensively
```

- **MUST** declare the `storage` permission to read settings.
- **MUST** tolerate a missing or malformed value. The store is also writable by
  the script, so a value can be anything.
- **MUST NOT** assume a change takes effect mid-session; settings are read at
  focus.

---

## 6. Review checklist

Before accepting a sub-app into `examples/` or the repository:

- [ ] Header block present and accurate (§4)
- [ ] Every loop bounded by a visible constant (§2.2)
- [ ] Display list measured and under 2 KB (log `pushToWatch: N B`)
- [ ] BACK handled
- [ ] Permissions declared are the ones actually used, and no more
- [ ] Settings, if any, validate defensively on read (§5.2)
- [ ] Opened on hardware at least once, and the watch survived

---

## 8. Host connectors (phone adapters)

JS sub-apps **MUST NOT** open sockets or touch Android APIs. Phone I/O goes
through named adapters. The isolate draws; the host fetches / binds.

Permissions are declared in `manifest.json`. Third-party packages get the same
binding ceiling as Official for every permission they declare.

| Binding | Permission | Events (`onEvent` source) | Notes |
|---|---|---|---|
| `slate.store` / settings | `storage` | — | Local key/value; settings are seeded here (§5) |
| `slate.timer` | — | `timer` | Interval clamped by governor |
| `slate.haptic` | — | — | Watch motor |
| `slate.phone.vibrate` | `vibrate` | — | Phone vibrator |
| `slate.location` | `location` | `location` | Subscribe / request; status states required |
| `slate.map` | `location` | `map` | Host draws OSM; script is thin controller |
| `slate.nav` | `navigation` | `nav` | Maneuver pushes |
| `slate.camera` | `camera` | `camera` | Host PATCH preview |
| `slate.news` | `news` | `news` | RSS/Atom list + text pages |
| `slate.media` | `media` | `media` | Now-playing + play/pause/skip (needs NLS) |
| `slate.http` | `http` | `http` | GET/POST; **requires** `http.allowedHosts` |
| `slate.weather` | `weather` | `weather` | Open-Meteo snapshot + marine sea/tides; phone GPS |

### 8.1 `slate.media`

```js
slate.media.subscribe()
slate.media.unsubscribe()
slate.media.play() / pause() / next() / previous()
// onEvent('media', …):
//   { type: 'nowPlaying', playing, title, artist, album, app }
//   { type: 'status', state: 'idle'|'denied'|'error', detail? }
```

Needs companion **Notification Listener** access (same as the shade).

### 8.2 `slate.http`

```json
"permissions": ["http"],
"http": { "allowedHosts": ["api.example.com"] }
```

```js
slate.http.get(url, { id: 'r1' })
slate.http.post(url, bodyString, { id: 'r2' })
slate.http.cancel(id)
slate.http.stop()
// onEvent('http', …):
//   { type: 'response', id, status, body }   // body capped ~64 KiB
//   { type: 'error', id, detail }
```

Host must match `allowedHosts` exactly (no wildcards). Prefer typed adapters
(`news`, `weather`) when the shape is fixed.

### 8.3 `slate.weather`

```js
slate.weather.fetch()                    // phone GPS (fresh when possible)
slate.weather.fetch({ lat: 48.85, lon: 2.35 })
slate.weather.stop()
// onEvent('weather', …):
//   { type: 'snapshot', tempC, weatherCode, label, precipMm, windMps, lat, lon,
//     seaLevelM?, nextHighTime?, nextHighM?, nextLowTime?, nextLowM? }
//   { type: 'status', state: 'loading'|'error', detail? }
```

### 8.4 Planned (see `docs/issue-prompts-open.md` I-17)

Calendar, JS notifications list/actions, phone battery, clipboard, share/
intents, alarms, contacts search, Health Connect, translate/TTS. Home/IoT
deferred — use `slate.http` from community packages first.

---

## 9. Related

- `docs/script-runtime.md` — isolate lifecycle, engine choice, IPC budget
- `docs/issue-prompts-open.md` — open defects, including I-17 connectors
- `CLAUDE.md` — the standing rules this document sits under
