# Notifications bridge (M9)

Phone-side adapter + Notifications sub-app. The watch never receives raw Android
notification objects — only SDP display lists (live UI) and channel-4 SYSTEM
records (retained store for phone-away reading).

## NotificationListenerService permission

NLS access is **not** a runtime permission. `requestPermissions()` cannot grant it.

1. Declare the service with `android.permission.BIND_NOTIFICATION_LISTENER_SERVICE`.
2. Deep-link with `Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS`.
3. User enables **Slate notifications** in the system list.
4. Companion UI shows NLS enabled/disabled (`SlateNotificationListener.isEnabled`).

## Capture pipeline

`SlateNotificationListener` → `NotifStore`:

- Drop our own FGS package; drop group summaries (`FLAG_GROUP_SUMMARY`).
- Dedup by notification key (upsert replaces).
- Skip empty title+text with no actions.
- Extract title / big-text / text / sub-text, importance, actions (incl. reply),
  synthetic **Dismiss** / **Snooze**.
- Map package → category + monogram via `NotifIconMapper`.

## Icon strategy — do we need an on-watch atlas?

**Hybrid. Small category atlas on the watch; phone does the mapping.**

| Layer | Role |
|-------|------|
| Phone | Maps `packageName` → category id + monogram. Live lists draw a chip + font letter (or `ICON` once packs exist). Never ships every app bitmap. |
| Watch | Tiny category atlas (message / call / mail / … — tens of glyphs) + built-in font for monograms. Retained-store rows store `category` + `monogram`, not PNGs. |
| Later (optional) | ASSET pushes for a few user-pinned app icons. |

Why not “atlas only on the phone”? Live UI could be phone-drawn forever, but the
**retained store** (channel 4) must remain readable when the phone is away. The
watch then needs *some* local art — a category atlas, not an app icon zoo.
Procedural monograms cover M9 without burning flash or BLE credit.

## Interrupt focus

High importance (`IMPORTANCE_HIGH`+) may call `requestFocus(…, INTERRUPT, …)` on
`slate.ref.notifications`. `NotifPrefs` gates this:

- Filter **off**: any HIGH+ may interrupt.
- Filter **on**: only packages on the allowlist.

## SYSTEM channel (4)

`SystemNotifCodec` payloads on `SdpFrame.CHAN_SYSTEM`:

| Op | Meaning |
|----|---------|
| `0x01` UPSERT | key, flags, category, monogram, title, text, when |
| `0x02` REMOVE | key |
| `0x03` CLEAR_ALL | — |

Firmware M10 interprets these into the local retained store; the companion
already pushes them so the wire path is live.

## Notifications app

`NotificationsApp` (`slate.ref.notifications`): M8 `KotlinSlateApp`.

- List: `SCROLL_REGION` so scroll is watch-local.
- Detail: title/body + dismiss / snooze / up to two native actions.
- Actions → `HostOutbound.AdapterCommand` → `NotifStore.invokeAction` (PendingIntent / cancel).

## Manual check

1. Enable NLS via companion button **1b**.
2. Connect watch / host emulator; start link FGS.
3. Post a messaging notification; confirm list + SYSTEM upsert dump.
4. Toggle interrupt allowlist; verify HIGH+ focus steal only when allowed.
