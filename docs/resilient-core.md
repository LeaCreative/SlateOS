# Resilient local core (M10)

Everything that must work with **no phone**: time, steps, alarms/timers, local
screens, tilt-to-wake, retained notifications, and session staleness.

Normative product scope: roadmap §3.4.

## RAM budgets (§3.2)

Measured by host test `local_core` (`static_assert` + runtime print):

| Block | Budget | Actual | Notes |
|-------|--------|--------|-------|
| Local screen state | **3072 B** | **3072 B** block (`State` payload **60 B** + reserve) | Face / nav / steps / settings / alert |
| Notification store | **4096 B** | **2564 B** | 16 entries; SYSTEM codec layout |

Both fit with headroom on the notification side; the local block is sized exactly to the budget so the linker/CI can see the allocation.

## Modules

| Module | Role |
|--------|------|
| `wall_clock` + `rtc_hw` | RTC1 @ 32768 Hz; CTS sync applies ppm drift; persist slot Clock |
| `bma42x` | Chip ID 0x11→421 / 0x13→425; HW step counter when feature config loaded; any-motion for tilt |
| `alarm_sched` | 8 alarms + 4 timers; flash-backed; haptic + Alert screen |
| `notif_store` | Channel 4 SYSTEM UPSERT/REMOVE/CLEAR (matches companion `SystemNotifCodec`) |
| `local_ui` / `local_core` | Face, Notifs (`SCROLL_REGION`), Settings, Charging, Alert, Disconnected |
| `persist` / `persist_nvmc` | Last internal flash page `0x0007F000` (4 KB) — pre-LittleFS |
| `session` (M7) | 2 missed heartbeats → stale; 5 → pop remote → watch face |

## Timekeeping

1. LFCLK XTAL already started in `SystemInit`.
2. RTC1 PRESCALER=0 → 32768 ticks/s; software overflow extends to 64-bit.
3. On CTS (or CONTROL op `0x20` + unix u32 LE until NimBLE CTS client lands): store
   `epoch_at_sync` + `ticks_at_sync`, update `drift_ppm` via EMA of residual error.
4. Blob survives reboot via NVMC persist.

## Steps + tilt

- Prefer the Bosch **feature-engine step counter** (no continuous accel sampling on the MCU).
- Pass a Bosch feature config blob into `bma::Driver::configure` (InfiniTime-compatible).
  Without the blob, chip detect + any-motion still run; step reads stay 0.
- Target ambient path: sensor LP + rare I2C polls (~2 s) + IRQ wake — design goal **&lt;20 µA** average for the step path (gate C measurement still required on hardware).
- Tilt sensitivity 0–7 retunes any-motion; Settings screen toggles enable / sensitivity.

## Staleness

Unchanged from M7 (`docs/session.md`): heartbeat period 2 s; **2 misses → stale overlay**;
**5 misses → pop remote stack** and reveal the local watch face. `local_core` mirrors the
stale flag on the face and marks retained notifs stale on link-down.

## Host tests

```powershell
.\scripts\run_host_tests.cmd local_core
```

Covers budgets, CTS drift + persist, SYSTEM notif codec, alarms/timers, alert haptic.
