# Image (JS sub-app)

Shows the Slate logo on the watch when focused.

## Why 63×63?

Firmware display lists are hard-capped at **4096 bytes** (`sdp::kMaxListBytes`).
Each push **replaces** the retained list and re-renders from black — there is no
framebuffer accumulate and `PATCH_REF` is a no-op — so the whole bitmap must fit
in one list. RGB332 at 63×63 is 3969 bytes; with `CLEAR` + `PATCH` + `COMMIT`
headers that still clears the 4 KiB gate. Larger images need a host-side tiled
streamer (like Camera) or a firmware change; this package stays pure downloaded JS.

## …but 63×63 is over the practical limit

The measured list is **3987 B**, and `docs/subapp-rules.md` §2 sets the practical
ceiling at **2048 B**. The parser is not the binding constraint — the credit
window is. The watch advertises its display buffer as 4096-or-0 rather than as
real free bytes (§3), so a screen this large lands or is dropped depending on
what happened before it, and the failure looks like flakiness in the app.

If it does not appear, look in the companion log for the reason and the numbers
before suspecting the script:

```
pushToWatch DROPPED: slate.image, 3987 B — credit: need 3987 B, window has 109 B
```

**Suggested fix, not applied.** The list is 18 B of headers plus one byte per
pixel, so the largest square that fits 2048 B is **45×45 = 2043 B** (46×46 comes
to 2134 B and misses). That is `SIZE` in `gen_logo.py` plus a regenerated array.
It has not been changed here because it visibly shrinks the logo, and the
artwork is the operator's call — see N-46 in `docs/issue-prompts-open.md` for
how the 4 KiB screen behaved on hardware.

## Install (no APK rebuild)

```powershell
Compress-Archive -Path manifest.json,main.js -DestinationPath slate.image.zip -Force
```

Open `slate.image.zip` on the phone with the Slate companion (`SideloadActivity`).
It appears in the repository / launcher as **Image**. Swipe right to leave.

## Regenerate the bitmap

Replace `logo.png`, then:

```powershell
python gen_logo.py
```
