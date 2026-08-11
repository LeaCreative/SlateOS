# Slate operator manual

What is on the watch, what every element means, and how to move between
screens. Written for the sealed PineTime — no SWD, no serial console, so the
screen is the only instrument you have.

**Applies to:** firmware `CFEDE90DE7F4` (`0.1.0-m16`) and later.
**Last updated:** 3 August 2026.

> Anything described as a *diagnostic* is only drawn in builds with
> `SLATE_DIAG_OVERLAY` enabled, which is every build you have flashed so far.
> A release build shows the clock, date, steps, battery and link block only.

---

## 1. Navigation

There is one physical control: the **side button**, plus touch swipes.

| Gesture | Effect |
|---|---|
| Face, swipe **down** | Notification shade |
| Notifs / detail, swipe **up** | Face (close shade) |
| Notif detail, swipe **left** (R→L) | Notification list (back) |
| Notifs, swipe **left** | Face (back) |
| Face, swipe **right** (L→R) | Settings |
| Settings, swipe **left** (R→L) | Face |
| Face, swipe **left** (linked) | Phone launcher |
| Phone sub-app, swipe **left** | Back (detail→list→close app); not launcher |
| Side button | Always returns toward the **Face** (from Notifs, detail, Settings, Call, Alert). Does **not** open Notifs from the Face |

Settings / Notifications / Apps share **outline** buttons (rounded stroke, chrome colour from companion Appearance). Face **bright** / **dim** colours theme the time+battery fill vs date/metrics/track; status glyphs stay green.

With **Heart rate** On in Settings, BPM appears on the face next to steps
(continuous measure while enabled).

**When the phone owns the screen** (a sub-app is showing), a short press means
*back*: the watch pops one remote screen, and when none are left it returns to
the watch face. It does this itself and tells the phone afterwards, so a busy
or wedged companion cannot trap you on a remote screen. The press is also sent
to the phone as a BACK event, so a sub-app that wants to handle its own
navigation still can.

Other button behaviour:

| Action | Result |
|---|---|
| Short press while asleep | Wakes the display; does **not** change screen |
| Short press on an Alert / Call screen | Returns straight to the Face |
| **Hold ~7 s** | Watchdog reset. The WDT is deliberately not petted while the button is held, so a wedged watch always reboots |
| Hold through boot until the screen turns **blue** | MCUBoot rollback to the previous image — the guaranteed recovery path, works even on a confirmed image |

---

## 2. The watch face

Vertical bands, top to bottom, on the 240×240 panel:

| Y range | Element |
|---|---|
| 0–7 | Trial-image marker (amber, only when unconfirmed) |
| 16–25 | Diagnostic line 1 |
| 28–37 | Diagnostic line 2 |
| 40–49 | OTA diagnostic line (only after a transfer has been attempted) |
| 56–95 | **HH:MM**, scale 8 |
| 110–124 | Date, `DD-MM-YY` |
| 136–150 | Step count |
| 176–180 | Remote-stale marker (amber) |
| 180–193 | Firmware version line |
| 186–195 | OTA progress bar + blink glyph (only during a transfer) |
| 196–203 | Battery gauge |
| 212–226 | Battery percent, charge bolt, display-list glyph, link block |

### 2.1 The clock

`HH:MM` in 24-hour form, and the date below it as `DD-MM-YY`.

The watch has **no timezone database**. It displays exactly the wall-clock the
companion sends it, and the companion sends *local* time — phone timezone
including DST, recomputed on every sync. If the face is wrong by a whole number
of hours, the companion is sending the wrong offset, not the watch.

`00:00` with a date of `01-01-70` means the clock has **never been set**: no
time sync has landed since the last cold boot. The time is re-sent 300 ms,
2 s and 8 s after the session goes Ready, and then every 15 minutes.

Between syncs the watch free-runs on RTC1 and applies a learned drift
correction (ppm), so it stays close even after days without a phone.

### 2.2 Steps

Step count for the current local day, resetting at local midnight. Hidden if
`face_show_steps` is off in settings.

### 2.3 Battery

- **Gauge** (y=196): a 200 px bar, filled proportionally to charge.
- **Percent** (y=212, left): `0`–`100`. Shows `--` when the reading is not yet
  valid — normally only in the first seconds after boot.
- **Charge bolt**: a small green lightning glyph appears to the right of the
  percent while the charger is detected (P0.12 low). The watch deliberately
  does **not** take over the screen while charging.

### 2.4 Link block

A 12×12 square in the bottom-right corner:

| Colour | Meaning |
|---|---|
| **Green** | A central is connected and the SDP session is up |
| **Dark grey** | No link |

This tracks the *session*, not merely the BLE connection.

### 2.4a Display-list glyph

A 12×12 square immediately left of the link block, reporting what the watch
does with screens pushed from the phone:

| Colour | Meaning |
|---|---|
| **Dark grey** | No display list has ever arrived |
| **Green**, alternating bright/dim | Lists are arriving and being applied — the alternation is the "still moving" cue |
| **Red** | A list arrived and the interpreter **rejected** it |

The distinction matters: a rejected list and one that never arrived look
identical on screen, but the first is a parser or display-list fault and the
second is a transport fault.

### 2.5 The amber bars — there are two, and they mean different things

This is the question that prompted this document, so both are spelled out.

**Top strip, full width, y=0–7 — trial image.**
The running firmware has been installed but **not yet confirmed**. MCUBoot will
revert to the previous image on the next reboot unless `IMAGE_OK` is written.
The watch writes it automatically after a short dwell with a central connected
(about 10 s), at which point the strip disappears for good. Seeing this after
an update is normal; seeing it persist means the confirm never happened — keep
the phone connected and it will clear.

**Short bar under the step count, y=176, 16×4 px — remote content is stale.**
The phone owns the screen but has stopped refreshing it: the last display list
is older than the session's freshness window. The watch keeps showing what it
has rather than blanking, and marks it as possibly out of date. It clears as
soon as a fresh display list arrives. On an idle watch face with no sub-app
pushing content this bar should not appear.

Mnemonic: **top bar = the firmware is provisional; middle bar = the content is
provisional.**

### 2.6 Version line

At y=180, small and dim:

```
0.1.0-m16 Aug 03 14:12
```

Firmware version, then the compiler build stamp (date and HH:MM). The build
stamp is what actually distinguishes two flashes of the same version — use it
to confirm an update landed. Diagnostic builds only.

---

## 3. Diagnostic lines on the face

Two dense numeric lines under the trial marker. Both are bring-up instruments;
neither is meant to be readable at a glance.

### Line 1 — `reset/uptime/paints/button/stall/ble.rc/catchup`

```
4/120/59/0/836/7.0/0
```

| Field | Example | Meaning |
|---|---|---|
| Reset reason | `4` | Raw nRF52 `RESETREAS` **bits**, not an index: `1` pin reset, `2` **watchdog**, `4` soft reset (`SYSRESETREQ` — what a DFU reboot does), `8` lockup, `0` power-on or brownout. Values combine, so `6` is watchdog + soft reset |
| Uptime | `120` | Seconds since boot |
| Paints | `59` | Full local repaints since boot. Should climb slowly; runaway growth means something is repainting needlessly |
| Button | `0` | Side button raw level right now |
| Stall | `836` | Worst observed app-loop stall, in ms |
| BLE state`.`rc | `7.0` | Bring-up stage reached, and the last NimBLE return code. `7.0` is fully up with no error; any non-zero after the dot is a failure at that stage |
| Tick catch-up | `0` | Times the RTC tick had to catch up. Non-zero means the tick was starved (see N-10) |

### Line 2 — `phase.ms/adc/mV/parse.render/dlOk.dlRej/touch.hit`

```
3.446/890/4171/0.220/2.0/0.0
```

| Field | Example | Meaning |
|---|---|---|
| Worst phase`.`ms | `7.413` | Which loop phase took longest, and how long |
| ADC raw | `891` | Raw SAADC count from the battery divider |
| Millivolts | `4176` | Converted battery voltage — compare against a multimeter |
| Parse`.`render ms | `0.220` | Worst display-list parse, and worst render |
| dlOk`.`dlRej | `2.0` | Display lists **applied** . **rejected** by the interpreter. `0.0` = none ever arrived (transport); `0.n` = arrived and refused (parser or list) |
| touch`.`hit | `0.0` | Touch events seen . those landing on a registered element. `0.0` = no touch detected at all; `n.0` = touches detected but hitting nothing; `n.n` = hits matched, so a TAP was emitted to the phone |

**Phase ids:** 1 drain, 2 input, 3 session/core tick, 4 paint, 5 battery/BAS,
6 the notify wait itself, 7 link-state transition repaint.

Phase **6** running long is the important one: it means the app task was
*ready but not scheduled* — something above it is hogging the CPU — rather than
any loop work being slow.

### Line 3 — OTA, `percent/last NAK/NAK count`

Only drawn once a transfer has been begun or refused, so the idle face keeps
its cheaper repaint.

NAK reasons: `0` ok, `1` busy, `2` bad message, `3` hash fail, `4` too large,
`5` no storage, `6` low battery, `7` yield, `8` **image unconfirmed** (the
running image is still on trial and refuses to accept a replacement — confirm
it first, or the known-good fallback would be lost).

---

## 4. During a firmware update

While a transfer is in flight the watch replaces the middle of the screen with:

```
UPDATING 47%
[========          ]
```

The app task otherwise stands back during a transfer — no repainting, no sensor
polling — because the external flash shares the SPI bus with the display. The
progress banner is deliberately a single band rather than a full repaint, and
updates every 2%.

When the transfer ends the normal face returns. The watch then reboots, swaps
the image, and comes back with the **amber trial strip** showing until it
confirms.

---

## 5. The notifications screen

Open with a **swipe down** from the face (not the side button).
Swipe **left** on a detail returns to the list; swipe **left** on the list (or
**up** anywhere in the shade) returns to the face. Leaving detail (button,
swipe left to the list, or swipe up) **deletes that stub on the watch only** —
the phone notification is unchanged. Dismissing on the phone removes the stub
via SYSTEM `REMOVE`.

New notifications wake the watch with **two short** vibrations and show an
amber glyph in the face status bar (left of the display-list / link blocks)
while any stubs remain.

The shade lists **title buttons** (from the SYSTEM stub store). Tap a row to
request the body from the phone; the detail screen shows the text.

**Incoming calls** (when the companion has phone-state permission, or as a
fallback from call-category notifications): three **long** vibrations and a
Call screen with the caller label for about 10 seconds. Display-only — no
answer/decline on the watch.

---

## 6. The settings screen

Second press from the face. Header `SETTINGS`, then a vertically scrollable
list (swipe up/down). Rows, top to bottom:

| Row | Setting |
|-----|---------|
| Raise wake | On/Off |
| Timeout | 10s … Never |
| Show steps | On/Off |
| Face diag | On/Off |
| Heart rate | On/Off — continuous measure; BPM on the face |

The same settings sync to the companion **Watch settings** screen.

One more press returns to the face.

---

## 7. Other screens

| Screen | When | Return |
|---|---|---|
| **Alert** | Raised by an alarm or an alert message. Large red digits: alert kind, then id | Any button press → Face |
| **Charging** | Legacy. Nothing navigates here any more — charging is shown by the bolt on the face | Button → Face |
| **Disconnected** | Shown when the remote UI owned the screen and the link dropped | Button → Face |

---

## 8. Quick fault guide

| Symptom | Likely meaning |
|---|---|
| `00:00` and `01-01-70` | No time sync has ever landed this boot. Check the companion log for `time sync → watch` |
| Clock off by whole hours | Companion sent the wrong offset (see N-26); the watch renders what it is given |
| Amber strip at the very top persists | Image still on trial; `IMAGE_OK` not yet written. Keep the phone connected |
| Amber bar under the steps | Phone owns the screen but has stopped refreshing it |
| Link block dark with the phone nearby | BLE connected but the SDP session never negotiated — check for HELLO in the companion log |
| Line 1 reset reason `4` after a reboot | Watchdog fired. Either the button was held, or the app task wedged |
| Line 2 phase `6` large | App task starved, not slow — something is hogging the CPU |
| OTA line NAK `8` | The running image is unconfirmed and is refusing an update, on purpose |
