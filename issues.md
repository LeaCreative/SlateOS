# Open issues — Slate / EvoTime

Last updated: 2026-07-31

Standing rule: **mirror InfiniTime** for boot, MCUBoot/DFU, flash map, WDT/button
reset, and BLE bring-up
([InfiniTime](https://github.com/InfiniTimeOrg/InfiniTime)). Differ on purpose only
at the SDP / companion JS display-list layer.

---

## Active / blocking

### I-1 — Sealed watch soft-brick (no BLE, no InfiniTime)

- **Status:** **Resolved** — watch recovered and running a confirmed Slate
  image; runbook and regression fence in tree, button-hold reset proven
- **Area:** Firmware / sealed DFU
- **Impact:** Watch unusable over the air until recovered
- **Notes:** After RTC-tick DFU (`slate-dfu.zip` SHA prefix `790822E97A78`; earlier
  tickless package `CC3F25A12AB2` also regressed), device shows Slate’s tiny
  glyphs, does not advertise, and InfiniTime does not return. Procedure:
  [`docs/recovery-sealed.md`](docs/recovery-sealed.md). In-tree WDT withhold
  mirrors InfiniTime; host test `wdt_hold` fences RR0 reload while held. User
  still needs discharge → pinecone → InfiniTime → fixed Slate flash (I-2).

### I-2 — Fixed Slate DFU package (post-recovery flash)

- **Status:** **Resolved** — flashed and confirmed on 31 July; the amber trial
  bar cleared, so `IMAGE_OK` is written and the install survives reset. This
  entry now just tracks the current staged package.
- **Area:** Firmware packaging
- **Artifact:** `build/dfu/slate-dfu.zip`
  - **SHA-256:** `5904578362CCEF557B8C32ED2A462DEB778704809E4DEA6CF6A3AE9789EEC57F`
  - **SHA-256 prefix (12):** `5904578362CC`
  - Built 2026-07-31 (night): `BOOTLOADER_PRESENT=ON`, `SLATE_HAS_NIMBLE=ON`,
    `imgtool create --slot-size 475136` (InfiniTime contract, unsigned)
  - Built 2026-08-01: everything verified to date (N-1/N-4/N-6/N-8, N-9
    identity, N-10/N-11 tick, N-12 battery) plus the **N-13 fix**: app task
    at BLE-host priority, repaint only on change, time-based diag cadence
  - Link map: FLASH ~122104 B / 475104 B; RAM ~60952 B / 64 KB
    (`__heap_end__` ~488 B below `__StackLimit`)
  - Supersedes `E54859689936` (N-12 verified, N-13 diagnosed from it),
    `C3AE3F15E408` (first confirmed durable install), and the earlier
    bring-up images `E95E6CD9676B`, `4324FC495E0C`, `2284BA304B8F`,
    `D7E86854B9D3`, `8B2D9054E9E7`
- **Fix asserts (all pass):**
  - (a) `src/wdt.cpp` — `pet()` returns before RR0 write when `button_raw()`
  - (b) `button.cpp` “Enable stays high” + `board::button_hw_init` OUTSET enable
  - (c) `port_rtc_tick.c` — one `xTaskIncrementTick` per TICK; no ISR catch-up
  - (d) `configUSE_TICKLESS_IDLE 0`
- **Flash order:** recovery/InfiniTime first ([`docs/recovery-sealed.md`](docs/recovery-sealed.md)),
  then this zip per [`docs/flash-sealed.md`](docs/flash-sealed.md). Confirm:
  amber bar → any central ~10 s (`kConfirmDwellMs` = 10000) → `IMAGE_OK`.
- **Do not flash:** `CC3F25A12AB2`, `790822E97A78`

### N-8 — Firmware no longer links: RAM heap/stack collision since N-1

- **Status:** Resolved (zero-copy `AppInbox` — borrows the reassembler buffer)
- **Area:** Linker / RAM budget (`ble::AppInbox`)
- **Impact:** Was: `build/dfu` failed `ASSERT(__StackLimit >= __heap_end__)`
  (`__heap_end__` 65040 B vs `__StackLimit` 61440 B — 3,600 B over). The
  I-2 zip had linked with only ~8 B margin; N-1's 4 KiB copy slot pushed
  heap past the 4 KiB MSP stack reserve, so no ARM image linked since.
- **Notes:** `AppInbox` no longer stores a 4 KiB copy: it borrows the
  reassembler's buffer ({ptr, channel, len} ≈ 12 B) and `Link::on_rx_write`
  gates ALL ingest (incl. DIAG) while a message is pending — fragments in
  that window are dropped and counted (`note_busy_drop`); CREDIT withheld
  until apply keeps well-behaved companions out of it. N-1 semantics
  unchanged (host never dispatches; app task drains). Both `build/dfu` and
  `build/dfu-prod` link again: RAM 60944 B / 64 KB (92.99 %),
  `__heap_end__` 0x2000EE10 → 496 B below `__StackLimit`. Host:
  `test_ble_app_inbox` (incl. new busy-window resync case).

### N-12 — Battery always reads 0 %: SAADC gain and resolution mis-set

- **Status:** **Resolved — verified on hardware.** Overlay read `831/3895`:
  831 × 4800/1024 = 3895 mV, a plausible cell voltage, and the displayed
  percentage tracks it. Multimeter cross-check still worth doing once (I-5).
- **Area:** `power.cpp::sample_battery_adc`, `battery.cpp::millivolts`
- **Impact:** Battery shows `0` (a *valid* 0, not `--`) on a charged cell.
  Also blocks I-13: SDP OTA and sealed DFU both gate on ≥30 % battery.
- **Root cause:** two independent register bugs.
  1. `CH0_CONFIG` GAIN field held **5**, which is gain **1**, not the 1/6 the
     comment claimed (encoding: 0=1/6 … 5=1). With the internal 0.6 V
     reference that puts full scale at 0.6 V while the divided battery pin
     sits near 2.0 V — every sample rails.
  2. `RESOLUTION` was set from a constant named `RES_12BIT` whose value was
     **1**, which is 10-bit (encoding: 0=8, 1=10, 2=12, 3=14). The name was
     wrong by 4x and silently agreed with nothing.
  The `adc * 2000 / 1241` conversion matched neither configuration, so the
  railed count converted to ~1.6 V and `percent_from_mv` floored it at 0.
- **Fix (mirror InfiniTime):** gain 1/4 + internal reference (full scale
  2.4 V on the pin = 4.8 V of battery through the 1:2 divider), 10-bit, and
  InfiniTime's `mV = raw * 8 * 600 / 1024`. Constants renamed so the
  encodings are written down. Host `test_battery` inverts the new formula.
- **Verify:** the diag overlay's new second line ends `/<adc raw>/<mV>`. A
  healthy cell should land ~3700–4200 mV; compare against a multimeter
  before trusting the curve breakpoints (the open item in I-5).

### N-29 — The local face repainted over every phone-pushed screen

- **Status:** Fix staged (`858A51CFDE6A`); awaiting hardware
- **Area:** `main.cpp` app loop, `Core::tick`
- **Impact:** A remote screen rendered correctly and was wiped within two
  seconds. Visible only as a brief flash, which read as "nothing happened".
- **Evidence:** the diag counter added for exactly this question read `3.0` —
  three display lists **applied**, zero rejected. So transport (N-28) and the
  interpreter were both already working; the screen was being overwritten
  after the fact.
- **Root cause:** three local repaint paths ran regardless of who owned the
  panel — the 2 s diag overlay refresh, the coalesced `take_paint_pending()`
  repaint, and `Core::tick`'s repaint on a minute change. Each called
  `show_current()`, which draws the **local** face.
- **Fix:** all three now check `g_local_owns_screen` / `local_owns_screen_`.
  Step counting, battery polling and session ticks continue as before — only
  painting is suppressed. `show_watch_face` clears the flag, so the button-back
  path (N-27) restores the face and its repaints together.

### N-31 — Touch is dead in every power state except Active

- **Status:** Open — root cause identified, mitigation shipped
- **Area:** `power.cpp` `buses_idle()` vs `cst816s` on TWIM1
- **Impact:** Taps do nothing. Diag `touch.hit` read `0.0` — the watch saw
  **no touch events at all**, so it was never TestApp or the companion.
- **Root cause:** `buses_idle()` disables TWIM1, and it is called on entry to
  **both** `Ambient` and `Glance`. The CST816S is on that bus. `input::poll()`
  checks the IRQ pin (a GPIO read, unaffected) and then reads the controller
  over I²C, which fails silently — `sample.valid` is false and no event is
  produced. Touch therefore only works in `State::Active`.
- **What triggered it:** the companion's `ClockApp` ran at `AMBIENT` priority,
  which put the session profile into ambient, which put the watch into power
  state `Ambient`. Removing ClockApp (owner's decision — the local face is
  authoritative) keeps the watch in `Active` and should restore touch.
- **Still wrong:** that is a mitigation, not a fix. Any future path into
  `Glance` or `Ambient` kills touch again, and a watch that cannot be touched
  cannot be woken by touch. The bus should be re-enabled on demand in
  `cst816s::poll()`, or the IRQ should drive a wake to `Active` first.
- **Related:** I-17 (display sleep) — see the correction there.

### I-17 — The display never sleeps under Slate

- **Status:** Open — raised 4 Aug
- **Area:** `power`, `local_core` wake/idle policy
- **Impact:** Battery life. The panel and backlight stay lit indefinitely,
  which also makes every current measurement taken so far unrepresentative.
- **Notes:** `wake_display()` exists and is called on input, but nothing drives
  the reverse transition on a timeout. `settings.wake_seconds` is carried in
  local state and shown on the settings screen, so the intended policy exists
  in data but is not enforced. InfiniTime dims then sleeps; mirror that.
- **Interacts with:** the OTA banner (a transfer involves no user input, so any
  sleep policy must keep the panel lit while `transfer_active()`), and I-3
  (tickless idle is the other half of the power story).

### N-28 — Every display push was dropped: CONTROL and DISPLAY sent back-to-back

- **Status:** Fix staged (companion only); awaiting hardware
- **Area:** `CompositorHost.pushToWatch`, `SdpWriteQueue`, `SlateGattClient`
- **Impact:** **No display list has ever reached the renderer.** The watch
  stayed on the local face with the companion reporting "Requested TestApp
  focus".
- **Evidence:** the list was sent and never applied — no CREDIT came back:
  ```
  22:29:59.627  sendMessage ch=0 bytes=1    ← pre-display CONTROL
  22:29:59.630  sendMessage ch=1 bytes=50   ← display list, same millisecond
  ```
- **Root cause:** `pushToWatch` sends `takePreDisplayControl()` on CONTROL and
  then immediately calls `pushDisplayList`, unconditionally, on **every** push.
  `AppInbox` holds one message and `ble_link` gates ingest while busy; the app
  task drains every 20 ms. The CONTROL message took the slot and the display
  list, 4 ms behind it, was dropped. Writes without response, so nothing
  retransmits. This is not a race that sometimes lost — the ordering is fixed,
  so display push could never have worked.
- **Fix:** real pacing rather than another per-message delay. `SdpWriteQueue`
  now tags each fragment with its channel and whether it closes a message, and
  the write pump leaves a 30 ms gap at message boundaries — comfortably past
  the 20 ms drain. Channel 5 (OTA) is exempt: it has credit-based flow control,
  never has more than one message outstanding, and pacing it would halve
  transfer throughput.
- **Lesson:** this is P-8, the hazard I filed as "mitigated but not solved"
  after N-25. I fixed the time sync by delaying that one message and left the
  general case; it broke the very next feature built on top of it. The
  mitigation should have been the fix.

### N-27 — A remote screen could not be dismissed; no way back to the watch face

- **Status:** Fix staged (`5904578362CC` + companion); awaiting hardware
- **Area:** `session::local_back`, companion `Compositor.dispatchInput`
- **Impact:** Once the phone owned the screen, the user was stuck there. The
  only escape was to drop the link.
- **Root cause:** two halves that each assumed the other would act.
  - `Manager::local_back` did **not** decrement `remote_depth_`. It only
    reported whether a remote screen existed; the watch emitted BACK on the
    wire and waited for the phone to pop.
  - The companion's fallback popped only `if (op == BACK && stack.size > 1)`,
    so a single focused app — the TestApp case, and the common one — was never
    dismissed. Nothing popped, nothing was pushed, and the watch kept
    displaying the remote screen indefinitely.
- **Fix:** the watch now pops its own stack. `local_back` decrements
  `remote_depth_` and, at zero, calls the `show_watch_face` hook directly. BACK
  is still emitted so the companion can follow, but returning to the watch face
  no longer depends on the phone being responsive. The companion's fallback now
  pops the last app too, so its view stays in step.
- **Found by:** reading the path before running it (I-13-style prep), not by
  hitting it on hardware.

### N-26 — Watch clock ran in UTC (four hours behind in UTC+4)

- **Status:** Fix staged (`CFEDE90DE7F4`); awaiting hardware
- **Area:** `CompositorHost.sendTimeSync`, `wall_clock`
- **Impact:** Face read 11:06 when local time was 15:06 (Seychelles, UTC+4).
- **Root cause:** the companion sent `System.currentTimeMillis()/1000` — a UTC
  epoch — and the watch renders what it is given. There is no timezone
  database on the watch and no offset field in TIME_SYNC, so nothing ever
  converted it.
- **Fix:** send **local** wall-clock (`now + TimeZone.getDefault().getOffset`).
  This is what CTS carries too, which is why the firmware entry point is
  already called `apply_cts_sync`. Recomputed per send, so the 15-minute
  resync picks up DST and travel. The log line now records local epoch, UTC
  epoch, offset and zone id.
- **Also:** `apply_cts_sync` no longer feeds a step change into the drift
  estimator. A DST boundary now arrives as a one-hour jump, and treating that
  as crystal drift would have poisoned `drift_ppm` for the session; residuals
  over `kMaxDriftErrorSec` (120 s) re-anchor instead.

### N-25 — Back-to-back CONTROL messages are silently dropped; clock stays at 1970

- **Status:** Fix staged (`E43C26CBE325`); awaiting hardware
- **Area:** `CompositorHost` Ready edge vs `ble::AppInbox`
- **Impact:** The watch clock never set itself even though the companion logged
  `time sync → watch: epoch=…` and the write returned `rc=0`.
- **Evidence:** three CONTROL messages inside 30 ms —
  `18.542 ch=0 bytes=20` (HELLO_ACCEPT), `18.550 ch=0 bytes=5` (TIME_SYNC),
  `18.572 ch=0 bytes=1` (heartbeat) — against an app task that drains every
  20 ms.
- **Root cause:** `AppInbox` holds exactly one message (N-8, zero-copy) and
  `ble_link` gates all ingest while it is busy. HELLO_ACCEPT took the slot;
  TIME_SYNC arrived 8 ms later and was dropped. These are **writes without
  response**, so nothing retransmits — and the periodic resync is 15 minutes
  away, so the face stays at 00:00 for the whole session.
- **Fix (companion):** do not send the time sync on the Ready edge. Send it
  300 ms later, and again at 2 s and 8 s. A redundant 5-byte write costs
  nothing next to another 15-minute wait.
- **Not fixed here:** the underlying asymmetry — the watch withholds CREDIT on
  a busy drop, but a write-without-response sender has no way to act on it.
  Any future multi-message CONTROL burst has the same hazard. Proper flow
  control on CONTROL is worth a dedicated issue.

### N-24b — Discovery watchdog fired during a healthy discovery

- **Status:** Fix staged (companion only)
- **Impact:** two `onServicesDiscovered` callbacks and a CCCD write rejected
  with `rc=201`, on every connect.
- **Root cause:** the N-24 watchdog tested `rxChar == null`, but `rxChar` is
  still null *while* discovery is in flight. At the 2 s mark a perfectly
  healthy discovery (started at 16.819, completed at 18.235) looked stalled.
- **Fix:** track whether discovery was **started**, not whether it finished.

### N-24 — Companion connects twice; discovery never runs, every send "not ready"

- **Status:** Fix staged (companion only); awaiting hardware
- **Area:** `SlateGattClient.connect`, CDM presence reconnect
- **Impact:** SDP OTA stalls on "Sending BEGIN" and nothing can be sent at
  all. The in-app log (added this session) showed it plainly: `connectGatt`,
  `requestMtu(247) submitted=true`, then `sendMessage: not ready` forever —
  and **no `onServicesDiscovered` line at any point**.
- **Root cause:** `connect()` began with `close()`, and CDM's
  `onDeviceAppeared` → "presence appeared — reconnect" fired ~250 ms after our
  own connect. The second `connect()` therefore closed a GATT that had
  `requestMtu()` outstanding. That request's `onMtuChanged` never arrived, and
  because service discovery is chained off `onMtuChanged`, discovery never
  started: `rxChar` stayed null, so every `sendMessage` failed "not ready".
- **Fix:**
  - Ignore `connect()` while `connecting` or already connected, instead of
    tearing down a connection in progress. The flag clears on discovery, on
    connection failure, on disconnect and in `closeInternal()`.
  - **Discovery watchdog:** if `onMtuChanged` has not arrived 2 s after a
    successful `requestMtu`, discover anyway. A dropped callback can no longer
    leave the link connected-but-unusable.
  - Rate-limit the "not ready" warning to once per 5 s and include why
    (`gatt`/`rx`/`connected`), since it previously buried the log.
- **Note:** this is why the earlier `logcat` showed only DIAG and SYSTEM
  sends — those are best-effort and simply failed; N-23 (HELLO_OFFER before
  subscribe) is a separate, real defect that this one was masking.

### N-23 — HELLO_OFFER sent before the central subscribes; SDP session never negotiated

- **Status:** Fix staged (`EB591620B5B0`); awaiting hardware
- **Area:** `ble_nimble.cpp` GAP events
- **Impact:** **The SDP session has never negotiated.** The watch clock stayed
  at 1970 because the companion never sent CONTROL traffic — and it never sent
  any because it never reached `Ready`. Display push would have been dead the
  same way; the link looked healthy (MTU 247, "ready to push", green link
  square) because all of that is GATT-level, below the session.
- **Evidence:** `adb logcat -s SlateLink` during a live connection showed only
  `ch=7` (DIAG) and `ch=4` (SYSTEM) sends — **not one `ch=0` CONTROL message**
  in minutes of uptime. No heartbeats, no time sync. That is the signature of
  a companion that never parsed HELLO_OFFER.
- **Root cause:** `notify_session_up()` fired on `BLE_GAP_EVENT_CONNECT`, so
  `session::on_link_up()` emitted HELLO_OFFER immediately. HELLO_OFFER is a
  **notification**, and the central does not write the TX CCCD until after
  service discovery — ~1.3 s later per the nRF Connect logs. NimBLE drops
  notifications to an unsubscribed client, so the offer went nowhere, every
  time. There was no `BLE_GAP_EVENT_SUBSCRIBE` handler at all.
- **Fix:** handle `BLE_GAP_EVENT_SUBSCRIBE` and start the session when the
  central subscribes to TX (`cur_notify`), ending it when it unsubscribes.
  Connect now only records the handle. This is what a GATT peripheral should
  do: nothing that depends on notifications may run before the client can
  receive them.
- **Note:** N-15 moved session-up *to* the connect event; before that it ran
  at the end of `run_negotiate()`, also pre-subscription. So this predates
  N-15 and was simply never visible while other failures dominated.

### N-22 — App task competes with an in-flight firmware transfer

- **Status:** Fix staged (`AFECC55F505D`); awaiting hardware
- **Area:** `app_loop`, `ble::dfu_busy()`
- **Impact:** A full-rate Nordic DFU (42 kB/s at MTU 247) hung silently at
  100 %: the link stayed up, nRF Connect kept its spinner, and no error ever
  arrived. NimBLE's mbuf pool is 6 × 292 B ≈ 1.7 KB; a 199 ms repaint holding
  the shared SPI bus is ~8 KB of inbound data at that rate, so the pool
  empties and the controller stops accepting. Worse, once it is empty the
  watch cannot allocate an mbuf to *report* the failure either —
  `ble_hs_mbuf_from_flat` returns null — so a real error becomes silence.
- **Fix:** while a firmware transfer is active — `ble::dfu_busy()` (Nordic
  DFU state != Idle) or `g_ota.transfer_active()` — the app loop skips the
  diag repaint, `Core::tick` (sensor and battery polling, whose ADC path ends
  in `buses_idle()`), and the BAS update. Session ticks still run so
  heartbeats cannot lapse. The app task simply stops competing for the SPI
  bus for the duration.
- **Not fixed here:** the mbuf budget itself (6 × 292 B is thin for a 247-byte
  MTU under sustained writes — `docs/ble.md` owns that maths), and the fact
  that DFU does flash I/O on the NimBLE host task at all. The real cure for
  both is InfiniTime's IRQ-driven `SpiMaster`, already flagged in the parity
  table. This makes the current design survivable, not correct.

### N-21 — `buses_idle()` disables SPI under an in-flight flash write

- **Status:** Fix staged (`9B814EBDE85B`). **Reasoned from the logs, not
  reproduced** — the race needs two tasks and real flash timing, so treat the
  next run as the test.
- **Area:** `power.cpp::buses_idle`, shared SPI0 bus
- **Impact:** With N-20 in, the DFU upload finally ran — **71.2 kB/s, 11 %** —
  then died with `Response received (Op Code = 3, Status = 6)` /
  `Remote DFU error: OPERATION FAILED`. That status comes from one place:
  `write_slot` returning false in the firmware-packet path.
- **Root cause:** `buses_idle()` guarded itself with `spi::is_acquired()`,
  which is a sample, not a lock. `xt25::wait_ready()` releases the bus between
  status polls, so through a 60–300 ms sector erase the flag reads false most
  of the time. Nordic DFU drives the flash from the **NimBLE host task** while
  the app task samples the battery every 10 s and calls `buses_idle()` — which
  then set `SPIM0.ENABLE = DISABLE` and put the flash into deep power-down
  under a live transfer. The next `spi::transmit` hit its 5 ms `EVENTS_END`
  timeout (the bounded wait from N-5, doing its job), `xt25::write_page`
  returned false, and the DFU engine answered OPERATION FAILED.
- **Fix:** `buses_idle()` now takes the bus mutex — `fs::sleep_flash()` first
  since it needs the bus itself, then `spi::acquire()` around the peripheral
  disable, which blocks until any transfer in flight on either task finishes.
- **Wider point for I-9:** the mutex was there and both drivers used it
  correctly; the leak was a *power-management* path touching the peripheral
  without it. Worth auditing every writer of `SPIM0.ENABLE` / `TWIM1.ENABLE`,
  not just the drivers.

### N-20 — Whole-slot erase blocks both update paths (Nordic DFU disconnects)

- **Status:** Fix staged (`7CBF667524F2`); awaiting hardware
- **Area:** `src/ota_slot.cpp`
- **Impact:** **Neither update path can complete.** nRF Connect logged
  `Firmware image size sent` then **29.4 s of silence** and
  `Device has disconnected`; SDP OTA sat on "Sending BEGIN". Both were the
  same stall, and it also explains the drop storms behind N-19.
- **Root cause:** `ota_slot::erase_all()` erased all 116 sectors of the
  secondary slot synchronously. At the XT25's 300 ms worst-case sector erase
  that is up to ~35 s in one call, and **both callers are in contexts that
  cannot disappear**: Nordic DFU runs `g_dfu.on_packet` in the GATT callback
  on the **NimBLE host task** (`ble_nimble.cpp:307`), so the link itself
  died; SDP OTA calls it from the app task at BEGIN, starving the drain so
  every chunk arriving meanwhile was dropped. The WDT pets inside the loop
  kept the watch alive, which is exactly why this looked like a comms fault
  rather than a flash one.
- **Fix — lazy erase, mirroring InfiniTime's `DfuImage`:** `erase_all()` now
  just resets an erase cursor, and `write()` erases forward only as far as
  the write needs. Both writers are strictly sequential from offset 0, so
  each sector is still erased exactly once before anything is written to it,
  and the cost becomes one ~60–300 ms erase per 4 KiB — comfortably inside a
  single chunk write. The trailer magic sits in the last sector, far past the
  image end, so it erases that one sector directly rather than letting the
  cursor walk the ~83-sector gap and reintroduce the stall.
- **Note:** this is the third defect in the same shape as N-14 — long work
  running in a context that must stay responsive. Worth an I-9 audit axis of
  its own: *what is the worst-case duration of anything reachable from a GATT
  callback or the app-loop drain?*

### N-19 — SDP OTA deadlocks after a dropped chunk (credit desync)

- **Status:** Fix staged (`66D6CB890F74` + companion); found by the I-13
  matrix on the first hardware run
- **Area:** `ota_xfer` credit accounting, `SlateOtaService` transfer loop
- **Impact:** OTA stalls permanently. Observed at exactly **512 / 133236 B**
  on the first attempt — one chunk through, then both sides waiting forever.
- **Root cause — an interaction, not a single bug.** The zero-copy link inbox
  (N-8) holds exactly one message, and `Link::on_rx_write` drops everything
  arriving while it is occupied. The companion, holding a 2048-byte credit,
  sends four 512-byte chunks back to back; the app task is still processing
  the first, so **three are dropped outright**. The watch ACKs 512 and the
  companion correctly rewinds `sentOffset`, but it has already decremented
  its own credit to 0, and the watch only re-advertised credit when its
  window fell below a quarter — which dropped traffic never reaches. Neither
  side can move. The protocol had no retransmit path at all.
- **Contributing observation:** the `BEGIN` slot erase blocks the drain for
  **~5.3 s** (overlay showed worst phase 1 = 5306 ms, worst stall 5763 ms,
  and recovered-ticks went to 1 for the first time). `ota_slot::erase_all`
  pets the WDT so the watch survives, but it guarantees a large drop window.
  It is 116 sector erases; slicing it across app-loop iterations would remove
  the worst offender. **Not done — filed here as follow-up.**
- **Fix, both sides:**
  - Watch: **the OTA window is now one chunk** (`kWindowBytes` =
    `kMaxBytesPerQuantum` = 512), matching the single-slot inbox, and credit
    is restored in full on every ACK. The exchange is lock-step: one chunk,
    ACK + credit, next chunk. A 2048-byte window meant the sender always put
    four chunks on the air when the watch could absorb one.
  - Companion: a chunk timeout is no longer fatal — it re-sends `BEGIN` (the
    resume handshake) and continues, bounded at `MAX_RESYNCS` = 20. Timeout
    cut from 15 s to 5 s now that a chunk is answered as soon as it drains.
- **Second run (2 Aug, watch still on the pre-fix image):** reached
  **10752/133252 B** then exhausted the resync budget. The arithmetic pins
  the mechanism exactly — 10752 = 21 × 512, one chunk recovered per 15 s
  resync, i.e. **one chunk of every four-chunk burst landed**. That 1-in-4
  ratio is precisely what a 2048-byte window into a one-message inbox
  predicts, and is why the window, not the recovery path, was the real bug.
- **Test:** `test_ota_xfer` now models the companion's own credit arithmetic
  and drops every third chunk. It reproduced the deadlock exactly (stalling
  with credit 0) before the fix and passes after, asserting that drops really
  occurred, the resync ran, and the slot matches byte-for-byte.

### N-18 — Full repaint costs ~611 ms: per-pixel tile rejection

- **Status:** **Resolved — verified on hardware.** Render **611 ms → 199 ms**
  (3.1x), worst app stall **3198 ms → 826 ms**, link transition (phase 7)
  **1873 ms → 398 ms**. Paints now track the 2 s diag cadence exactly
  (451 paints in 989 s). Remaining 199 ms is ~115 ms SPI + ~84 ms in-band
  rasterisation, so further gains need the SPI itself: skip pushing tiles
  whose content did not change, then the parity table's sanctioned
  escalation to InfiniTime's IRQ-driven `SpiMaster`.
- **Area:** `src/renderer.cpp` draw ops
- **Impact:** One face repaint took ~611 ms (measured on hardware), which is
  what made the app task disappear for half a second at a time and left the
  link feeling fragile. It also bounds how fast SDP OTA (I-13) can run, since
  OTA chunks drain on the same task.
- **Root cause:** `put_pixel()` applies the tile filter **per pixel**. A full
  repaint runs every op once per tile row, so a full-screen `CLEAR` iterated
  all 57,600 pixels on each of the 30 passes and discarded 29/30 of them
  *after* doing the bounds, clip and tile arithmetic — about **1.7 million
  rejected calls per frame** for the clear alone, before any text. SPI is
  only ~115 ms of the total; the rest was rejected pixel work.
- **Fix:** draw ops now consult the active tile band first — `fill_rect`,
  the three blits, line, circle, round-rect and arc either skip entirely or
  clamp their row loop to the visible rows. `put_pixel` still enforces the
  filter, so culling is a pure fast path and output is unchanged; dirty
  marking still covers the whole shape.
- **Coverage:** the renderer had **no** host test at all. `test_renderer_cull`
  now checks a full-screen fill reaches all 30 tiles, a single-tile rect
  lands only in its tile, a rect straddling a tile boundary paints the exact
  rows on both sides, blit source rows stay aligned when the top is culled
  (an off-by-one here would slice glyphs), and unfiltered drawing is
  untouched.
- **Expected:** the overlay's `<parse>.<render>` field should drop sharply
  from `0.611`. What remains is ~115 ms of SPI plus the surviving in-band
  rasterisation. If it is still too slow, the next steps are skipping the SPI
  push for tiles whose content did not change, and the parity table's
  sanctioned escalation to InfiniTime's IRQ-driven `SpiMaster`.

### N-17 — Slate service UUID published byte-scrambled

- **Status:** **Resolved — verified on hardware.** nRF Connect reads the
  service as `e979acfb-c338-0000-a962-e96e4cf078f3`; the companion reports
  `ATT MTU 247`, `subscribed TX(+STATUS); ready to push`, stable connect /
  reconnect / disconnect. **This completes the SDP link end-to-end.**
  Fixed in `B7A58CFAC9B6`. **First attempt `FAE65071E5A7`
  changed nothing on the wire** — `include/slate_uuids.hpp` is not what the
  radio uses. `src/ble_nimble.cpp` carried its own hardcoded
  `BLE_UUID128_INIT` bytes (line ~311), so fixing the header alone was
  invisible. Both are now corrected and bound together by `static_assert`,
  and the built binary is verified to contain the corrected bytes and none
  of the old ones.
- **Duplicate-definition tell:** in the *same file*, `uuid_dfu_*` was written
  as a correct full reversal while `uuid_svc`/`rx`/`tx`/`status` used the
  mixed-endian layout. That is exactly why the Nordic DFU service always
  worked and Slate's own service never did — the strongest possible hint,
  sitting twenty lines apart, unnoticed until the byte-level comparison.
- **Area:** `include/slate_uuids.hpp`
- **Impact:** **This is why "Slate service not found" survived N-16.** The
  service was in the GATT table all along after that fix — under the wrong
  UUID, so no central filtering on the real one could match it. It also
  explains why the *advertisement* carried a UUID the companion ignored.
- **Proof:** nRF Connect listed the 128-bit service as
  `f378f04c-6ee9-62a9-44fa-0000e979acfb`. Reversing the shipped `kService`
  array reproduces that string exactly.
- **Root cause:** NimBLE stores a 128-bit UUID as the text form reversed **in
  full** — byte 0 is the last byte of the string. The arrays reversed only
  *within* each group (`time_low`, `time_mid`, …), which is the Microsoft
  GUID mixed-endian convention, not NimBLE's. The comment even claimed
  "little-endian byte order for NimBLE", so the intent was right and the
  layout was wrong — invisible to review because the bytes *look* plausible.
- **Fix:** all four arrays rewritten as full reversals of their canonical
  strings, with the discriminant in bytes 8–9. New host test `test_uuids`
  formats each array back to text and pins it to the same string the
  companion hard-codes (`SlateUuids.kt`), and fails explicitly if the
  mixed-endian layout ever returns.
- **Note:** the DFU and stock services were unaffected (16-bit or supplied by
  NimBLE), which is exactly why everything *except* Slate's own service
  appeared to work.

### N-16 — GATT services registered too late; never in the attribute table

- **Status:** **Resolved — verified on hardware.** nRF Connect lists both the
  Slate service and the Nordic DFU service in the attribute table. The
  residual "service not found" after the first attempt was N-17 (wrong UUID),
  not this. Historical note: `F27CF0DD8D5A` still reported it from the
  companion, but see the Android GATT cache note below — the phone caches
  the service list per bonded device, and it had cached a database with no
  Slate service in it. **Forget the watch in Android Bluetooth settings and
  remove the CDM association before retrying**, or the phone will keep
  serving the stale table no matter what the firmware does.
- **Area:** `ble_nimble.cpp` `on_sync` / `start_stack`
- **Impact:** **The Slate service has never been discoverable.** With N-15
  clearing the way, the companion finally got far enough to say so:
  `ATT MTU: 247 (target 247)`, `MTU event: 247 status=0`, then
  `Error: Slate service not found`. The watch advertised the 128-bit service
  UUID while the GATT table did not contain it — so every session, every
  benchmark and every OTA attempt was doomed regardless of the link.
- **Root cause:** `ble_gatts_count_cfg` / `ble_gatts_add_svcs` for `g_svcs`
  (the Slate service **and** the Nordic DFU service beside it) ran inside the
  `on_sync` callback. `ble_hs_start()` calls `ble_gatts_start()` and *then*
  syncs, and per `ble_gatt.h`, queued services "get registered when
  `ble_gatts_start()` is called". Registering after that point queues them
  into a table that is never built again. The stock services (GAP, GATT, BAS,
  DIS) were unaffected because their `*_init()` calls already ran in
  `start_stack`, before the host — which is exactly why discovery appeared to
  work while our own service was missing.
- **Fix:** register `g_svcs` in `start_stack`, alongside the stock service
  inits and before `nimble_port_freertos_init`, so `ble_gatts_start()` builds
  the table from everything. `on_sync` now only sets identity and starts
  advertising. Failure codes 90/91 still report a registration problem.
- **On-device proof:** `on_sync` now calls `ble_gatts_find_svc()` on the
  Slate UUID after the table is built and reports **state 96** if it is
  absent. That separates "firmware still broken" from "phone cached the old
  table". Bring-up failure codes are now sticky, too — a registration
  failure in `start_stack` was previously overwritten by the later
  "advertising" mark, so the overlay could not have shown it.
- **Android GATT cache:** Android caches the discovered service database per
  bonded device and will keep returning the cached one after the peripheral
  changes. Because the watch genuinely had no Slate service until now, that
  stale cache is the expected state. Clearing it needs the device forgotten
  in Bluetooth settings (plus the CDM association removed), or a Bluetooth
  off/on. Worth surfacing in the companion under I-15 as an explicit
  remediation, since any firmware GATT change can trigger it.
- **Lesson for I-9:** this is the third defect in code that had never run.
  Init **ordering** against the stock NimBLE/InfiniTime sequence is its own
  audit axis — not just what we call, but when.

### N-15 — Mirror InfiniTime's connect path (stop driving negotiation)

- **Status:** **Resolved — verified on hardware.** `ATT MTU: 247 (target 247)`
  with `MTU event: 247 status=0`; the central drives the exchange and the link
  survives it.
- **Area:** `ble_nimble.cpp` GAP path, `local_core.cpp` link transitions
- **Impact:** With N-14 in place the tearing stopped, but the link still could
  not be held: MTU reached 247 for a moment, then the connection dropped, and
  association never completed. Overlay read `7.1873/820/3843/0.630` — worst
  phase **7** (link transition) at **1873 ms**, parse **0 ms**, render
  **630 ms**. So a single connect cost ~1.9 s of repainting (two to three
  full frames: `session.on_link_up` → `show_watch_face`, then
  `Core::on_link_up`), landing exactly while the central was discovering.
- **Root cause — a Slate invention at the mirror layer:** `run_negotiate()`
  ran **synchronously inside the `BLE_GAP_EVENT_CONNECT` callback**, issuing
  an ATT MTU exchange, a DLE request, a 2M PHY request and a connection
  parameter update, all at once, from the peripheral, while the central was
  mid-discovery. A mirrored peripheral does none of that: it publishes a
  preferred MTU and reacts to what the central does.
- **Fix:**
  - `ble_att_set_preferred_mtu(247)` once at sync; the central's own
    exchange then lands at 247 and arrives as `BLE_GAP_EVENT_MTU`, which is
    now recorded into STATUS. `BLE_GAP_EVENT_CONN_UPDATE` likewise records
    the interval the central chose.
  - The connect handler records the handle and signals session-up. Nothing
    else.
  - `run_negotiate()` is kept and exposed as `ble::negotiate_now()` for the
    roadmap A/B/D gates, which do need those parameters actively raised —
    but it must be called from the app task after discovery settles, never
    from a GAP callback.
  - Link transitions no longer repaint. `Core::on_link_up/on_link_down` and
    `show_watch_face` mark the face pending; `app_loop` coalesces them into
    one repaint outside the callback.
- **Note:** `parse = 0 ms` disproves the earlier "33 display-list parses"
  theory — the whole 630 ms is rasterise + SPI push. That is the next target
  (see N-13 follow-up and the parity row on `SpiMaster`).

### N-14 — Session hooks render on the NimBLE host task (tearing, dropped link)

- **Status:** **Resolved — verified on hardware.** No tearing since; connect,
  reconnect and disconnect are all stable with the render off the host task.
- **Area:** `main.cpp` session hooks, `ble_nimble.cpp` GAP event path
- **Impact:** On `DFD04D130924`: torn/corrupt face during reconnect, the
  companion showing `Connected: true` for a split second before dropping,
  association impossible. Looks like an N-13 regression; it is not — it is a
  pre-existing bug that the N-13 priority change exposed.
- **Root cause:** `notify_session_up()` / `notify_session_down()` are called
  from the GAP event path **on the NimBLE host task**
  (`ble_nimble.cpp:222` and the DISCONNECT case). The hooks ran
  `g_session.on_link_*()` and `g_core.on_link_*()` directly, and both end in
  `show_current()` → a full ~1.2 s parse+render. So every connect and
  disconnect did a 1.2 s blocking render on the host task, which (a) stalled
  all ATT traffic exactly when the central was negotiating, and (b) drove the
  interpreter and renderer concurrently with the app task's own repaint —
  hence the tearing. N-1 moved the *message* path off the host task but left
  these two hooks behind; with the app task previously at a higher priority
  the collisions were rarer, and equal priority made them constant.
- **Fix:** the hooks now only publish `{state, seq}` and wake the app task;
  `app_loop` applies the transition (loop phase 7) before draining messages,
  so session work, HELLO_OFFER and repaints all happen on the app task.
  Rapid flapping collapses to the latest state. Same rule as N-1.
- **Note:** the DIAG bench render callback has the same shape and also runs
  on the host task. It is inert in release (`SLATE_BLE_DIAG=0`) but should be
  moved before diag builds are used again.

### N-13 — App loop iterates ~30x slower than designed → BLE unusable

- **Status:** **Resolved — verified on hardware.** Worst app stall
  3198 ms → 826 ms and paints now track the 2 s diag cadence exactly; the
  remaining per-frame cost was the renderer itself (N-18, 611 ms → 199 ms).
- **Diagnosis:** overlay read `3.1236` — worst phase **3** (session/core
  tick) at **1236 ms**, not phase 6. So the app task was never starved; it
  was doing over a second of blocking work. Two causes:
  1. `Interpreter::render_retained_to_display` re-parses the **entire**
     display list once per tile — 30 tiles, plus the validate, side-effect
     and meta passes: 33 full parses and 30 rasterise+SPI passes per
     repaint. `Core::tick` triggered that unconditionally every 2 s, and
     the diag overlay triggered another every 16 iterations.
  2. The app task ran at `tskIDLE_PRIORITY + 2`, **above** the NimBLE host
     task at +1. That render therefore blocked all ATT traffic, which is
     why MTU stayed at 23, service discovery never completed, the link
     dropped on supervision timeout, and every benchmark failed to send
     (`THRU_START send failed — is GATT ready?`).
- **Fix:** app task moved to `tskIDLE_PRIORITY + 1`, equal to the NimBLE
  host, so time slicing interleaves them (and mirroring InfiniTime, where
  MAIN/DisplayApp sit at or below the BLE host); `Core::tick` repaints only
  when the step count or the displayed minute actually changed; the diag
  overlay repaint is time-based (2 s) instead of per-16-iterations. Worst
  parse+render is now surfaced as the overlay's 4th second-line field.
- **Follow-up (not done):** the per-tile re-parse is the underlying cost and
  wants dirty-rect rendering so an unchanged region is not re-rasterised 30
  times. Do that under I-10 rather than blind — it needs the renderer's
  dirty tracking to be trustworthy first.

### N-11 — FreeRTOS tick IRQ ran at priority 0 (double-shifted priority)

- **Status:** **Resolved — verified on hardware** (reset reason back to 4, no
  watchdog resets, recovered-ticks 0, no cyan asserts)
- **Area:** `port/nrf52/src/port_rtc_tick.c` `vPortSetupTimerInterrupt`
- **Impact:** Latent since the RTC1 tick port was written; the most likely
  true cause of the N-10 reboot loop (and a plausible contributor to older
  "unexplained" instability). Found when the N-10 build painted **cyan
  `397105A1` / `0000037A`** = `port.c:890`,
  `configASSERT(ucCurrentPriority >= ucMaxSysCallPriority)` — the N-10
  catch-up added `xTaskGetTickCountFromISR()`, the first self-validating
  FromISR call in that handler, which immediately reported the bad priority.
- **Root cause:** `NVIC_SetPriority(portNRF_RTC_IRQn,
  configKERNEL_INTERRUPT_PRIORITY)`. CMSIS shifts internally
  (`p << (8 − __NVIC_PRIO_BITS)`), but `configKERNEL_INTERRUPT_PRIORITY` is
  the **already-shifted** register form FreeRTOS writes straight into SHPR
  (`7 << 5` = 224). Shifted twice: `(224 << 5) & 0xFF` = **0**. So the tick
  ran at priority 0 — above NimBLE's RADIO (5) and above
  `configMAX_SYSCALL_INTERRUPT_PRIORITY` (3). `portENTER_CRITICAL` raises
  BASEPRI to 3, which cannot mask priority 0, so `xTaskIncrementTick()`
  could mutate the delayed-task lists **inside another task's critical
  section** — matching the observed late/never-scheduled app task
  (~460 ms iterations) better than tick coalescing did.
- **Fix:** pass the raw number (`configLIBRARY_LOWEST_INTERRUPT_PRIORITY`,
  7) plus a `_Static_assert` that the argument is a raw priority, so this
  cannot regress. Tick now sits at priority 7 as intended — which is also
  what makes N-10's catch-up genuinely necessary (below RADIO at 5, TICK
  events really can coalesce). Both fixes ship together.
- **Audited:** the only other `NVIC_SetPriority` in Slate code is via
  `nrfx_glue` (unused); GPIOTE (`cst816s.cpp`) runs at default priority 0
  but only sets a flag and clears events — no FreeRTOS API calls, so it is
  safe, though worth revisiting under I-9 since it can pre-empt the radio.

### N-10 — FreeRTOS tick loses time under BLE load → watchdog reboot loop

- **Status:** **Resolved — verified on hardware** (recovered-ticks reads 0, so
  with N-11 fixed the tick no longer falls behind; the catch-up remains as
  insurance and for the tickless path, I-3)
- **Area:** `port/nrf52/src/port_rtc_tick.c` (RTC1 tick), mirror-rule gap
- **Impact:** With the radio finally live (N-9), the watch reboots every
  ~30 s. Diag read `6/22/3/0/940/7.0`: reset reason **6 = soft|watchdog**
  (bit 2 set for the first time), advertising healthy, but only **3 paints
  in 22 s of real time** — the app loop's `pdMS_TO_TICKS(20)` wait was
  taking ~460 ms, i.e. the FreeRTOS tick running ~20x slow. nRF Connect
  sees the watch, connects, then `GATT CONN TIMEOUT (0x8)` ~37 s later —
  that is the watch rebooting under it, not a GATT fault.
- **Root cause:** the RTC1 handler runs at kernel (lowest) interrupt
  priority, below NimBLE's RADIO ISR (priority 5) and level with its
  RTC0/TIMER0 ISRs, so once the radio is live it is regularly held off past
  the 0.98 ms tick period. RTC TICK events **coalesce** (the peripheral just
  re-sets one flag), so "one increment per TICK event" silently drops time
  and the FreeRTOS clock free-runs slow. Everything scheduled in ticks
  stretches in real time — including `app_loop`'s 20 ms wait and therefore
  the WDT pet cadence — until the bootloader's real-time 7 s dog bites.
  `mono_ms()` reads the RTC1 COUNTER directly, which is why uptime looked
  sane while tick-scheduled work crawled.
- **Fix:** restore InfiniTime's catch-up (`diff = (COUNTER −
  xTaskGetTickCount()) & MAXTICKS`, stock `port_cmsis_systick.c`
  behaviour — the mirror-rule default) with the hard guards the file header
  demanded: diff in the top half of the range = tick is ahead (step once,
  never ~16M — the original underflow-spin regression), diff 0 = step once,
  and ≤128 ticks stepped per ISR so the handler stays short. Recovered
  ticks are counted and shown as the diag overlay's **7th field**.
- **Verify:** overlay is now
  `reset/uptime/paints/button/stall/ble.rc/recovered_ticks`. Expect no
  watchdog resets (reset reason back to 4), paints ≈ uptime ÷ 0.32, and a
  climbing recovered-ticks value quantifying what used to be lost. Tickless
  (I-3) inherits this fix — note it in the I-9 parity table as aligned.

### N-9 — Slate up but radio silent: advertising never starts on hardware

- **Status:** **Resolved — verified on hardware.** Diag read `7.0`,
  nRF Connect sees "SLATE" at `E8:01:34:22:08:89` (static-random, top bits
  `11` as required) and connects. Reboot loop that followed is N-10.
- **Area:** `ble_nimble` / NimBLE port bring-up
- **Impact:** No BLE at all on `D7E86854B9D3`/`2284BA304B8F`: nothing in
  nRF Connect, CDM finds nothing, IMAGE_OK cannot confirm. UI healthy;
  button-hold revert **verified working** on hardware (first field proof of
  the R-3/N-2 WDT fix).
- **Root cause (from telemetry image `2284BA304B8F`, diag read `93.21`):**
  `ble_gap_adv_start` → `BLE_HS_ENOADDR` (21). Controller synced and GATT
  registered fine, but no identity address was ever configured — nRF52 has
  no public IEEE address, and the standalone port never set the
  FICR-derived static-random identity (same class of gap as the missing
  `ble_ll_init` sysinit call).
- **Fix:** `on_sync` forms the static-random address from `FICR.DEVICEADDR`
  (top two bits set per spec; stable across boots so CDM associations and
  bonds survive), `ble_hs_id_set_rnd` + `ble_hs_id_infer_auto`, and both
  adv starts use the inferred own-addr type. Failure code 95 added for the
  identity path. Telemetry stays in tree: diag overlay 6th field `state.rc`
  (`ble::bringup_snapshot`, `include/ble_gatt.hpp`): 7.0 = advertising,
  1–6 = bring-up stage reached, 90–95 = named failure with rc.

### I-3 — FreeRTOS tickless idle on RTC1 not soak-tested

- **Status:** Open (disabled by default)
- **Area:** `port/nrf52/src/port_rtc_tick.c`, `config/FreeRTOSConfig.h`
- **Impact:** Higher idle current until safe to enable; wrong enable bricks sealed units
- **Notes:** Enabling tickless previously led to paints ≈4 then InfiniTime at ~7 s
  (ISR catch-up underflow after `vTaskStepTick`). Suppress path was adjusted;
  `configUSE_TICKLESS_IDLE` remains `0` until sealed soak proves hold-reset and
  BLE stay healthy. See `docs/freertos-tickless.md`.

---

## Firmware UX / correctness

### I-4 — Local UI glyphs unreadable (3×5 TEXT)

- **Status:** Resolved (`text_big` / TEXT_SCALED on all local screens)
- **Area:** `src/local_ui.cpp`
- **Impact:** Was: watchface / notifs / settings / alert unreadable at arm’s length
- **Notes:** Face already used scale 8/3; notifs/settings/alert/disconnected now
  use `text_big` (≥3 body, 8 for hero). `text_digits` removed. Host:
  `test_local_ui` (6-row notifs ≈296 B ≤ 512 `dl_buf_`).

### I-5 — Battery percent shows 0 while charging

- **Status:** Mitigated (InfiniTime curve + unknown + charge-edge resample)
- **Area:** `battery` / `battery_hw` / `local_core` / face
- **Impact:** Was: charge bolt + numeric 0 until first/60s SAADC
- **Notes:** `kPercentUnknown` (0xFF) → face `--`; charge-pin edge + 10 s ADC via
  `Core::sample_battery` hook; InfiniTime 6-point mV curve + 99% charge clamp +
  hysteresis. OTA/DFU treat unknown as blocked. Host: `test_battery`. Validate
  `adc*2000/1241` vs multimeter before trusting curve breakpoints absolutely.

### N-1 — SDP parse/render on NimBLE host task (link→app handoff)

- **Status:** Resolved (I-10 stage 1)
- **Area:** `ble_link` / `ble_app_inbox` / `main` app task
- **Impact:** Was: GATT write callback ran session + interpreter + ST7789 SPI on
  the BLE host task, racing app-task local UI (torn renders; ~115 ms host stalls)
- **Notes:** Host only reassembles + `AppInbox::try_push` (zero-copy since
  N-8: inbox borrows the reassembler buffer, ingest gated while pending;
  busy → drop-and-count). App task `drain_app_messages` then
  `on_app_message`; CREDIT after deferred `on_display` apply. Wake via
  `xTaskNotifyGive`. Host: `test_ble_app_inbox`. DIAG loopback on producer
  except during the pending window.

### N-4 — Frame reassembler: per-channel state but one shared 4 KiB buffer

- **Status:** Resolved (single-in-flight rule, zero RAM)
- **Area:** `sdp_frame` reassembler + companion frame writer
- **Impact:** Was: interleaved multi-fragment messages on different channels
  silently corrupted each other in the shared `buf_`
- **Notes:** §4.2 now specifies single-in-flight: a FIRST on any channel
  (incl. FIRST|LAST) aborts an in-flight reassembly on another channel
  (reject-and-resync; `Reassembler::preempt_drop_count()`, folded into
  `Link::drop_count()`). Channel-rejected frames (6/7-release) don't abort.
  Companion: `SdpWriteQueue` (sdp-core) owns per-channel TX seq and enqueues
  each message's fragments atomically (replaces `ChannelSeq` + raw queue in
  `SlateGattClient`); `SdpReassembler.kt` mirrors the abort rule. Host:
  `test_sdp_frame` interleave cases; Kotlin: `SdpWriteQueueTest` (incl.
  concurrent enqueue), `SdpFrameTest` preempt cases. Wire format unchanged.

### N-6 — HELLO_OFFER encoder had unguarded buffer writes

- **Status:** Resolved (capacity-checked writes + build-time size guard)
- **Area:** `session.cpp` HELLO_OFFER encoder
- **Impact:** Was: `put_u16(tmp + n, …)` / `fill_opcode_bitmap(tmp + n)` wrote
  unbounded, and `push` silently truncated at capacity — catalog growth would
  have become a stack overflow or a corrupt offer
- **Notes:** All writes now go through `push`/`push_u16`/`push_bytes` with an
  `ok` flag; any overflow returns 0 (like the local-UI writer `W`).
  `kHelloOfferMaxBytes` (worst case from `profile::kCatalogCount` ×
  (4 + name ≤ `kMaxProfileNameLen`) + fixed fields) is `static_assert`ed
  against `kHelloOfferBufBytes` (160) in `session.hpp`, so catalog growth
  fails the build. Host: `test_session` byte-exact golden (84 B) + overflow
  cases; Kotlin: `HelloOfferGoldenTest` pins the same bytes on
  `SessionTestFixtures.encodeHelloOffer` and drives them through
  `SessionClient.parseHelloOffer` (decoder coverage).

### I-6 — IMAGE_OK confirm vs single BLE connection

- **Status:** Mitigated (dwell mechanism kept as documented InfiniTime exception)
- **Area:** `boot_util` / companion / field process
- **Impact:** Easy to miss confirm; next reset reverts unconfirmed image
- **Notes:** Amber bar = unconfirmed. Any central for ~10 s writes `IMAGE_OK`.
  Companion queries CONTROL `CONFIRM_STATUS` (0xE1) for in-app countdown;
  `LinkContention` warns when another app holds GATT. See
  `docs/flash-sealed.md` step 8 and `docs/infinitime-parity.md`.

### I-7 — Nordic DFU “failed” on successful reboot/swap

- **Status:** Open
- **Area:** Companion sealed DFU client
- **Impact:** False failure after a transfer that actually swapped
- **Notes:** Watch resets into MCUBoot; phone often reports failure. Operators
  should check the watch face / InfiniTime, not only the app toast.

### I-8 — Sealed DFU target classification is heuristic

- **Status:** Open (accepted residual risk)
- **Area:** Companion preflight
- **Impact:** Wrong zip/target on a non-InfiniTime BL map could brick
- **Notes:** MCUBoot has no BLE; InfiniTime vs PineDFU/SoftDevice heuristics can
  reject bad zips but cannot prove BL layout. See `docs/flash-sealed.md`.

---

## Architecture / deferred

### I-9 — Audit remaining InfiniTime low-level divergences

- **Status:** Open (partial table started)
- **Area:** Boot, DFU, BLE advertising, power, RTC ownership
- **Impact:** Process debt; avoid inventing sealed-watch “improvements”
- **Notes:** Living table: [`docs/infinitime-parity.md`](docs/infinitime-parity.md).
  **WDT pet site** marked **aligned** (app-task-only reload; tick/idle do not pet).
  Continue audit (adv name/intervals, DFU service, sleep, confirm UX exception, etc.).

### I-10 — FreeRTOS task topology split deferred

- **Status:** Deferred (stage 1 done — link→app handoff)
- **Area:** Scheduler / roadmap §3.1
- **Impact:** Single app task + NimBLE ll/host only
- **Notes:** N-1 shipped: GATT → `AppInbox` → app-task drain (no host-task
  render). Remaining `display` / `link` / `sensors` / `system` split not started.

### I-11 — HRS GATT / HRS3300 driver deferred

- **Status:** **RESOLVED** — HRS3300 driver + InfiniTime PPG + `hr` FreeRTOS
  task + GATT `0x180D` + synced `hr_enabled` (wire v2) + scrollable settings.
- **Area:** BLE / sensors
- **Impact:** Heart-rate on demand when settings gate is On
- **Notes:** Default `hr_enabled=0` keeps ambient budget; CCCD / local HR
  screen start measurement only while enabled.

### I-12 — GATT CTS deferred

- **Status:** Deferred
- **Area:** Time sync
- **Impact:** No standard CTS client
- **Notes:** Time uses CONTROL `0x20` (`TimeSync.kt`); not GATT CTS.

### I-13 — Channel-5 SDP OTA not proven on durable Slate

- **Status:** **RESOLVED on hardware (3 Aug).** Two consecutive Slate→Slate
  updates over channel 5, no InfiniTime round-trip:
  `EB591620B5B0` → `E43C26CBE325` → `CFEDE90DE7F4`.
  - 134180 B at ~46 kB/s, every 512 B chunk ACKed and credited, **zero NAKs
    and zero resyncs** across the whole transfer.
  - Reboot, swap, `status=8` supervision timeout, reconnect, session
    negotiated, and `IMAGE_OK` written after the connected dwell —
    `watch image ON TRIAL — 4905ms remaining` then
    `watch image CONFIRMED (IMAGE_OK written)`.
  - Watch shows live `UPDATING nn%` during the transfer and its own version
    string afterwards, so neither end has to be inferred any more.
- **Remaining from the original prompt:** the deliberate-failure rows of
  `docs/ota-verification.md` (mid-transfer disconnect, corrupt chunk on the
  wire, refusal while unconfirmed) are still only proven in host tests, not
  on hardware.

- **Prep, for reference (1 Aug):**
  - **The IMAGE_OK guard did not exist** and has been added. `ota_xfer` takes
    an `image_confirmed` hook (wired to `!boot::needs_confirm()`) and refuses
    `BEGIN` with the new `NakReason::Unconfirmed` (8) while the running image
    is on trial — otherwise a revert lands on unvalidated firmware and the
    known-good fallback is gone. Companion decodes 8 with an explanatory
    message; `fromCode` already degraded unknown values safely.
  - **Instrumented before running:** overlay third line
    `<percent>/<last NAK>/<NAK count>` (drawn only once OTA is touched, so
    the idle repaint stays cheap), RTT logs for every refusal and each 10 %
    with byte offsets, and a companion per-chunk log with offsets and credit.
  - **Host tests extended:** full 4 KB transfer in 256 B chunks with forced
    resumption at 25/50/75 %, stale-chunk re-sync, byte-for-byte slot
    comparison, a corrupted chunk caught at COMMIT, and the unconfirmed
    refusal. 18/18 host suites pass.
  - **Matrix written:** `docs/ota-verification.md` — 11 rows, preconditions,
    instrumentation, throughput notes and recovery.
  - Prerequisites confirmed: battery reads true (N-12), link stable at MTU
    247 (N-15/N-16/N-17), running image confirmed. The Nordic DFU service is
    also reachable *on Slate* now, so Slate→Slate flashing no longer needs an
    InfiniTime round-trip.
- **Area:** Firmware `ota_xfer` + companion OTA UI
- **Impact:** Preferred post-install update path unverified end-to-end on hardware
- **Notes:** Implementation exists; blocked on stable boot + `IMAGE_OK` + companion
  link. Prefer after I-1/I-2 cleared.

### I-14 — RAM headroom tight (~93% in prod link map)

- **Status:** Watch
- **Area:** Linker / CI RAM gate
- **Impact:** NimBLE / heap regressions can trip cyan asserts or CI
- **Notes:** Hard gate: static + heap ≤ 54 KiB with ≥ 6 KiB slack. Heap bumped
  historically for NimBLE eventq; keep monitoring. N-1's `AppInbox` is
  zero-copy since N-8 (~12 B metadata, not 4 KiB); current link:
  RAM 60944 B / 64 KB, 496 B below the MSP stack reserve — the margin is
  thin, so any static growth needs a map check.

---

## Companion / ops

### I-15 — CDM / reconnect useless when watch is not advertising

- **Status:** Open (expected limitation; UX unclear)
- **Area:** Companion CDM + link service
- **Impact:** User confusion during soft-brick / InfiniTime-only recovery
- **Notes:** Associate / Start reconnect cannot find a silent radio. Sealed
  installer path must steer users to InfiniTime/recovery CDM + DFU.

### I-16 — Multi-app BLE contention (Gadgetbridge et al.)

- **Status:** Mitigated (`LinkContention` + troubleshooting screen)
- **Area:** Companion + `ble_nimble` adv resume
- **Impact:** Blocks HELLO, confirm, OTA, DFU when another central holds the slot
- **Notes:** `SLATE_BLE_MAX_CONNECTIONS 1`. Instant GATT occupancy + bonded/
  connect-fail hints; app-specific remediation copy in `LinkContention`.
  Firmware resumes advertising on failed connect (`ble_gap_adv_policy`).
  See companion Troubleshooting and `docs/flash-sealed.md` step 7.

---

## Resolved recently (keep for context; remove when stale)

| ID | Summary | Resolution |
|---|---|---|
| R-1 | SysTick tickless never wakes on nRF52 System ON sleep | RTC1 tick port; SysTick tickless `#error` |
| R-2 | Charging forced `Screen::Charging` tiny green glyphs | Stay on face; charge icon beside battery |
| R-3 | WDT always petted from tick (wedged app never resets) | Button withhold in `wdt::pet()`; tick/idle **no longer pet** — only `app_loop` (+ flash helpers / future POST_SLEEP). Host: `wdt_hold`, `wdt_app_pet`. See `docs/infinitime-parity.md`. |
| R-4 | Button strobe too short → hold never seen | Enable left high (InfiniTime); simple sense read |
| R-5 | SPI `EVENTS_END` infinite spin (freeze with N-2) | Bounded wait + SPIM recover + bool errors; dirty-tile retry; always-on CS checks (`spi_bus`) |

---

## Suggested next order of work

Rewritten 3 Aug. The previous list (I-1 recovery → I-2 flash → IMAGE_OK →
I-4 glyphs → I-13) is **complete**. Bring-up is over: the watch boots, holds a
link at MTU 247, negotiates an SDP session, tells the time, and updates itself
over the air without an InfiniTime round-trip.

1. **Push a display list from the phone.** Everything above this point was
   plumbing. Nothing has yet exercised the actual product thesis — companion
   composes, watch renders. The transport is proven; the payload is not.
2. **I-15** — companion UX when the watch is not advertising. The last thing
   still capable of stranding a user with no path forward.
3. **I-7 / I-8** — Nordic DFU reports failure on a successful swap, and the
   sealed-DFU target classification is heuristic. Both are now secondary,
   since SDP OTA is the primary update path (I-13).
4. **I-9** — InfiniTime divergence audit. Nearly every defect this session
   (N-9 … N-17) was a place Slate deviated. Worth doing once, deliberately,
   rather than one crash at a time.
5. **I-3** tickless soak and **I-14** RAM headroom — both are "watch" items
   that only bite under sustained runtime, which we now finally have.

Deferred by choice: **I-10** (task split), **I-12** (CTS —
largely moot now that TIME_SYNC works and carries local time, see N-26).
**I-11** (HRS) is resolved — see above.
