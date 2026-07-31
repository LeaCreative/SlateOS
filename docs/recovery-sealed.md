# Recover a sealed PineTime from a Slate soft-brick (no BLE)

Sealed units have **no SWD**. If Slate boots but never advertises, and the side
button cannot force a reset, the only field path is: starve the battery → catch
the InfiniTime MCUBoot pinecone on charger boot → hold for **blue** (revert) or
**red** (recovery firmware) → DFU known-good InfiniTime → only then flash a
fixed Slate package.

Normal first install / update (when InfiniTime or recovery already runs): see
[`docs/flash-sealed.md`](flash-sealed.md). OTA / MCUBoot map details:
[`docs/ota.md`](ota.md).

Bootloader behaviour (authoritative):
[pinetime-mcuboot-bootloader](https://github.com/InfiniTimeOrg/pinetime-mcuboot-bootloader)
and InfiniTime’s user docs. On each reset the bootloader draws the pine cone for
about **5 seconds**. While that logo is drawing:

| Keep holding until | Action |
|---|---|
| Logo fills **blue** | Force rollback to the **previous** firmware image (usually InfiniTime), even if the current image was validated |
| Logo fills **red** | Load **recovery firmware** from external flash (stripped InfiniTime: BLE + OTA/DFU only) |

Release after the colour change. Do **not** release on the early white/green
draw and press again — that boots the (broken) application.

---

## Known-bricking Slate DFU packages

Do **not** reflash these. Identify by SHA-256 prefix of `slate-dfu.zip`:

| SHA-256 prefix | Failure mode |
|---|---|
| `CC3F25A12AB2` | RTC1 FreeRTOS tick **with tickless idle on**; tick ISR catch-up underflow → ISR spin → bootloader WDT → InfiniTime after ~7 s (paints stuck ~4) |
| `790822E97A78` | RTC1 tick, tickless off, but **WDT always petted from the FreeRTOS tick path** even while the button was held. Soft-brick: tiny local glyphs, **no BLE**, InfiniTime-style hold reset **unavailable** |

### Fixed package (post I-2)

| SHA-256 prefix | Path |
|---|---|
| `8B2D9054E9E7` | `build/dfu/slate-dfu.zip` (full: `8B2D9054E9E7017A9E24C25FE4E01C4608C0A912ADC765D886E2623480A3E540`) |

Built with WDT withhold-on-hold, button enable left high, RTC1 tick with
`configUSE_TICKLESS_IDLE 0`, and no ISR COUNTER catch-up. Flash only after
InfiniTime/recovery is healthy; then confirm amber → 10 s central → `IMAGE_OK`.

After recovery, flash only a package built from a tree that passes
`tests/host` `wdt_hold` and matches the fix chain in
[Why this can't recur](#why-this-cant-recur). Prefer the `8B2D9054E9E7` artifact
above when available.

---

## Symptoms that mean this runbook

- Screen shows Slate’s sparse / tiny glyphs (not the animated pinecone).
- Phone Bluetooth scan, Slate CDM associate, and “start / reconnect link” find
  **nothing**.
- Holding the side button for 8+ seconds does **nothing** (broken image kept
  feeding the bootloader WDT from the tick ISR).
- InfiniTime does not return on its own.

If the pinecone **does** appear and hold-to-blue/red works, skip discharge and
jump to [Step 3](#step-3--bootloader-gesture-blue-or-red).

---

## Step 1 — Force a cold boot when hold-reset is dead

1. **Unplug** the charger.
2. Leave the watch until it is fully flat (hours if the radio is quiet; longer
   if something is spinning). There is no field way to cut the cell on a sealed
   unit.
3. Do **not** keep trying companion CDM / reconnect — the radio is not up.

Optional: while waiting, stage on the phone a known-good **InfiniTime** DFU zip
(`pinetime-mcuboot-app-dfu-*.zip`) and, separately, a **fixed** Slate
`slate-dfu.zip` (not the SHAs above). Disable Gadgetbridge / Amazfish
auto-reconnect so they cannot steal the only BLE slot later.

---

## Step 2 — Catch the pinecone on charger boot

1. Plug the charger in. The bootloader should run and show the **pine cone**
   (white filling toward green over ~5 s).
2. **Start holding the side button as soon as the logo appears** (or keep holding
   through power-on if you can time it).
3. The logo must change colour **before** you release:
   - Prefer **red** if InfiniTime itself is gone or flaky → recovery firmware.
   - Prefer **blue** if you only need to roll back to the previous InfiniTime
     image.
4. If you miss the window and Slate’s glyphs return: unplug, wait for another
   drain (or try a long hold only if you are on a **fixed** image that withholds
   WDT — see below), then retry.

Camera tip: early white-on-bright frames are easy to underexpose; trust the
**colour change** (blue/red), not a washed photo of white.

---

## Step 3 — Bootloader gesture (blue or red)

### Blue — revert to previous firmware

Hold through the pinecone until **blue**, release. MCUBoot reloads the previous
slot (typically InfiniTime). Confirm InfiniTime boots and advertises.

### Red — recovery firmware

Hold through the pinecone until **red**, release. Recovery advertises and speaks
Nordic legacy DFU (`0x1530`). Use Slate companion **Select InfiniTime / recovery
(CDM)**, or nRF Connect / Gadgetbridge DFU.

Recovery is for catastrophic app failure. After using it, still install a full
InfiniTime (or fixed Slate) and validate / confirm as appropriate.

---

## Step 4 — DFU known-good InfiniTime first

From InfiniTime **or** red recovery:

1. Battery ≥30%, preferably charging.
2. Enable firmware updates if InfiniTime settings are available.
3. DFU a known-good **InfiniTime** application zip (not SoftDevice, not a
   bootloader reloader, not the bad Slate SHAs above).
4. Let the watch reboot into InfiniTime. Validate firmware in InfiniTime
   Settings if that UI is present so MCUBoot will not keep reverting.

Only when InfiniTime (or recovery→InfiniTime) is stable and advertising, continue.

Procedure details and companion sealed installer: [`docs/flash-sealed.md`](flash-sealed.md).

---

## Step 5 — Flash a fixed Slate package

1. Build or obtain a Slate `slate-dfu.zip` from a tree that includes the WDT
   withhold fix and the safe RTC tick (tickless **off**, no ISR COUNTER
   catch-up). Prefer SHA prefix `8B2D9054E9E7`
   (`build/dfu/slate-dfu.zip`). Confirm the zip SHA is **not** `CC3F25A12AB2`
   or `790822E97A78`.
2. Install per [`docs/flash-sealed.md`](flash-sealed.md) (companion sealed DFU
   or Gadgetbridge / nRF Connect).
3. On first Slate boot: amber trial bar → connect **any** BLE central for ~10 s
   → bar clears (`IMAGE_OK`). Stop other apps from holding the link first.
4. Prove hold-to-reset: hold side button ~7–8 s → pinecone. Release at white/green
   to reboot Slate, or continue to blue/red if you need rollback/recovery again.

---

## Why this can't recur

### Root cause of `790822E97A78`

InfiniTime’s `SystemTask::Work()` reloads the watchdog **only** when the side
button reads released:

```cpp
if (nrf_gpio_pin_read(PinMap::Button) == 0) {
  watchdog.Reload();
}
```

(InfiniTime leaves `ButtonEnable` high; pressed = HIGH, so `== 0` means up.)

Slate image `790822E97A78` petted the bootloader WDT from the FreeRTOS **tick**
path **unconditionally**. A wedged app (no BLE, few paints) still ran the tick
ISR, so RR0 kept being reloaded while the button was held. The bootloader dog
never fired; `poll_reboot_button()` never ran if app/idle were stuck → no reset.

### In-tree fix (verified funnel)

Single choke point for the reload write: `slate::wdt::pet()` in `src/wdt.cpp`.

1. If `board::button_raw()` is true (pressed), **return without writing RR0**.
2. Otherwise write the Nordic reload magic `0x6E524635` to WDT RR0.

**Who may call it (InfiniTime SystemTask analogue):**

| Call site | Path |
|---|---|
| `app_loop` | `src/main.cpp` → `pet_service()` → `pet()` each iteration |
| Long flash helpers | `ota_slot::erase_all`, `xt25::wait_ready` → `pet()` |
| Pre-scheduler init / `busy_wait_ms` | `pet()` / `pet_service()` |
| `configPOST_SLEEP_PROCESSING` | `slate_wdt_pet()` → `pet()` (future tickless; sleeps ≤ 1 s) |

**Who must not pet:**

| Call site | Behaviour |
|---|---|
| `vApplicationTickHook` | Stall telemetry (`g_max_stall_ms`) only |
| `vApplicationIdleHook` | Empty — idle running while app is blocked must not feed the dog |

There is **no** other writer of WDT RR0 under `src/`. Button enable stays high
(InfiniTime-style) so `button_raw()` is a plain GPIO read.

### Regression fence

Host tests `wdt_hold` and `wdt_app_pet` (`tests/host/`) compile `src/wdt.cpp`
against a fake `board::button_raw()` and a host-mapped RR0 cell. They fail if
RR0 reloads while the button is pressed, and `wdt_app_pet` fails if simulated
tick/idle hooks reload while the app is wedged.

---

## What not to do

- Do not OTA a bootloader / reloader zip on a sealed watch.
- Do not flash SoftDevice / PineDFU / wasp packages expecting this MCUBoot map.
- Do not confirm (`IMAGE_OK`) a build that never advertises.
- Do not rely on companion CDM while the radio is dead — recover via pinecone
  first.
