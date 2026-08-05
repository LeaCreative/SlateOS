# Slate companion — what every control does

**Companion `0.1.4-p4 (build 5)`.** Written 5 August 2026.

> **Accuracy note.** Entries marked ✔ were confirmed by reading the handler.
> Entries marked ~ are inferred from the label and the activity/service they
> launch, and have **not** been traced through. Treat ~ as a claim to verify,
> not as documentation.

---

## 1. Setup — do these in order, once

| Button | What it does |
|---|---|
| **1. Grant permissions** ✔ | Requests the runtime BLE and notification permissions. Everything else fails silently without these |
| **1b. Notification access (system settings)** ✔ | Deep-links to the system screen for `NotificationListenerService`. This permission **cannot** be granted from inside the app |
| **2. Associate watch (CDM)** ✔ | CompanionDeviceManager association with the `watch` profile. Required before the link service can reconnect on its own |
| **3. Start / reconnect link service** ✔ | Starts `LinkForegroundService` — the foreground service that owns the GATT connection. Nothing reaches the watch until this runs |

---

## 2. Sub-apps — the screens the phone pushes to the watch

| Button | What it does |
|---|---|
| **Open TestApp** ✔ | Focuses the **Kotlin** reference app: black background, white 160×64 rect with `EMIT_TOUCH`, tap counter. A transport probe, *not* a product test |
| **Open Notifications** ✔ | Focuses the **Kotlin** `NotificationsApp` and pushes the current notification snapshot to it |
| **Open Timer (JS sub-app)** ~ | Focuses the `timer` JS sub-app (`companion/examples/timer`). **This is the real product path** and the one P-1 should close against |
| **Open Navigation (JS)** ~ | Focuses the `navigation` JS sub-app; bridges turn-by-turn from another phone app |
| **Open Camera (JS + PATCH)** ~ | Focuses the `camera` JS sub-app; exercises the `PATCH` opcode (bitmap blits) rather than vector primitives |

Priorities matter here: `ClockApp` is **AMBIENT** and re-renders while buried at
1/min, so it overwrites whatever is focused (N-34). `NotificationsApp` is raised
at **INTERRUPT** by incoming notifications and pre-empts any screen.

---

## 3. Notifications

| Button | What it does |
|---|---|
| **Interrupt filter: allow this phone's SMS pkgs** ~ | Whitelists the SMS/messaging packages so they may raise an INTERRUPT screen |
| **Interrupt filter: allow all HIGH+** ~ | Allows any notification at HIGH importance or above to interrupt |

**Two independent notification paths exist** — see §6.

---

## 4. Firmware and diagnostics

| Button | What it does |
|---|---|
| **Install Slate on sealed PineTime** ✔ | Nordic legacy DFU (service `0x1530`) for a watch running InfiniTime or its bootloader. The bootstrap path |
| **Update Slate firmware (SDP OTA)** ✔ | Channel-5 SDP OTA, Slate→Slate. The **primary** update path (I-13) |
| **Ping RTT (DIAG ch7)** ✔ | Round-trip probe on the DIAG channel. Bring-up instrumentation |
| **Benchmarks (gates A / B / D)** ~ | Runs the A/B/D performance gates |
| **Troubleshooting (BLE one-slot)** ~ | Explains contention when another central (Gadgetbridge, nRF Connect) holds the watch's single connection slot |
| **View log** ✔ | In-app log. Buttons: **Pause/Resume**, **Copy**, **Share**, **Clear**, **Tail on/off**. Pause freezes the view *and* what Copy/Share capture |
| **Disconnect** ✔ | Drops the GATT link |

---

## 5. Scripting and platform

| Button | What it does |
|---|---|
| **Script console** ~ | `DevConsoleActivity` — evaluate JS against the sub-app runtime |
| **Sub-app repository** ~ | `RepoActivity` — browse/install JS sub-apps |
| **Background reliability settings** ~ | Guidance for OEM battery optimisers that kill the foreground service |
| **Slate app battery / autostart settings** ~ | Deep-links to this app's OEM battery/autostart page |

---

## 6. How notifications reach the watch — two paths, and the open decision

**Path A — SYSTEM channel (channel 4), local store.**
`SystemNotifCodec` upserts each notification into a store *on the watch*.
The watch's own local Notifications screen renders it — monogram, title
**length**, stale flag. Numeric only, because local font 0 has no letters. This
path works with no sub-app running and survives the phone going away. It is the
one that has been working all along; it produced the screen you photographed.

**Path B — display list from a sub-app.**
`NotificationsApp` (currently **Kotlin**) builds a readable screen and pushes it
on channel 1, raised at INTERRUPT when a notification arrives.

**The open decision — and it matches your instinct:** Path B should be a **JS
sub-app**, not Kotlin. The bridge (`NotificationListenerService` → `NotifStore`)
stays native, because it must be; but the *rendering* belongs in a sub-app like
any other. That keeps one rule for all screens and makes notification layout
replaceable without shipping an APK.

Nothing blocks this technically — the JS builder already emits byte-identical
display lists (`JsRuntimeTest`) and `textScaled` now exists for legible text.
What is missing is a JS notifications app and a binding to expose `NotifStore`
to it, permission-gated like every other sub-app binding.

Path A should stay regardless: it is the offline fallback and needs no phone.

---

## 7. Ready for tomorrow — JS sub-app testing

**Blocking first:** frame drops in the reassembler (`src/sdp_frame.cpp`).
Diag line 3 read `538.82`. Until that is fixed, a screen lands only once per
connection, whatever pushes it.

**Then, in order:**

1. **Set `ClockApp` to `RefreshPolicy.Manual`** — cheap, no flash. Stops the
   ambient clock overwriting a focused sub-app (N-34).
2. **Open Timer (JS sub-app)** — the real P-1 acceptance. Expect the screen to
   render and persist.
3. **Tap its button element.** Blocked on N-31: touch is dead outside
   `State::Active` because `buses_idle()` disables the TWIM1 bus the CST816S
   sits on. Fix that before tap testing, or the result is meaningless.
4. **Side button** should return to the watch face (N-27 path, working).
5. **Then Navigation and Camera** — Camera additionally exercises `PATCH`.

**Keep TestApp** as a transport probe: when a JS sub-app misbehaves, it
distinguishes a runtime problem from a transport one in a single press.
