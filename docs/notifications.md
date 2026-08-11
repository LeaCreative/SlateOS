# Notifications bridge

Phone-side adapter + local watch notification shade. The watch never receives
raw Android notification objects — only channel-4 SYSTEM records (stubs and
on-demand bodies) and, for calls, `CALL_ALERT` / `CALL_END`.

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
- Extract title / big-text / text / sub-text, importance, actions.
- Resolve **app label** via `PackageManager` (retained on the phone-side item; stubs use notification title).
- Map package → category + monogram via `NotifIconMapper`.

## Watch UX (local firmware)

| Moment | Behaviour |
|--------|-----------|
| New stub arrives | Wake + **DOUBLE** haptic (two short pulses); amber glyph on face status bar. Burst/reconnect UPSERTs coalesce to **one** DOUBLE within ~2.5 s |
| Swipe **down** on face | Open Notifications shade (title buttons) |
| Tap a row | Watch sends INPUT `NOTIF_REQ` (0xE2); companion replies SYSTEM `BODY`; detail screen |
| Leave detail (button / swipe left to list / swipe up) | Stub **removed on watch only** (phone notification untouched) |
| Phone dismisses (NLS remove) | SYSTEM `REMOVE` clears the watch stub |
| Side button | Returns to face (does **not** cycle Face→Notifs→Settings) |

Kotlin `NotificationsApp` INTERRUPT focus is **disabled**; the shade is local.

Local list rows show the notification **title** (same field as Kotlin NotificationsApp).

Detail shows `...` while waiting for BODY; if the body never arrives (unknown key / link), it falls through to **No text** after a few seconds.

Stub keys are capped at **64** UTF-8 bytes (matching the codec). Longer Android keys are looked up by prefix on the phone.

## SYSTEM channel (4)

`SystemNotifCodec` payloads on `SdpFrame.CHAN_SYSTEM`:

| Op | Dir | Meaning |
|----|-----|---------|
| `0x01` UPSERT | phone→watch | Stub: key, flags, category, monogram, **notification title**, empty text, when |
| `0x02` REMOVE | phone→watch | Drop stub |
| `0x03` CLEAR_ALL | phone→watch | — |
| `0x04` BODY | phone→watch | key + body text (on demand) |
| `0x06` CALL_ALERT | phone→watch | caller label |
| `0x07` CALL_END | phone→watch | dismiss call screen |

Watch → phone body request uses INPUT extension `0xE2` (`NOTIF_REQ`): `[op][key_len][key…]`.

On `NOTIF_REQ`, the companion records the key in `watchClearedKeys` so reconnect bulk sync does not re-UPSERT items already read on the watch until the phone also removes them.

Stub store: up to 24 entries, no resident body text (detail buffer is separate on the watch).

## Incoming calls (display-only)

1. Prefer `CallMonitor` + `READ_PHONE_STATE` (TelephonyCallback / PhoneStateListener).
2. If permission is missing: ongoing **CALL**-category notifications raise `CALL_ALERT` as fallback (no double-fire when telephony is active).
3. Watch: Call screen with caller label, **TRIPLE_LONG** haptic (three long pulses), auto-dismiss after **10 s** (or button / `CALL_END`). No answer/decline.

## Manual check

1. Enable NLS via companion settings.
2. Optionally grant phone state for real ring alerts.
3. Connect watch; post a messaging notification — expect two short vibes + face glyph.
4. Swipe down → tap app name → see body → button back; stub gone on watch, still on phone.
5. Dismiss on phone → stub disappears on watch.
6. Incoming call → three long vibes + caller screen for ~10 s.
