# BLE transport (M5)

## UUID base

Generated with `uuidgen` / .NET `Guid`:

| | UUID |
|---|---|
| **Base** | `e979acfb-c338-44fa-a962-e96e4cf078f3` |
| Service | `e979acfb-c338-0000-a962-e96e4cf078f3` |
| RX (Write Without Response) | `e979acfb-c338-0001-a962-e96e4cf078f3` |
| TX (Notify) | `e979acfb-c338-0002-a962-e96e4cf078f3` |
| STATUS (Read, Notify) | `e979acfb-c338-0003-a962-e96e4cf078f3` |

Discriminant is `time_mid` (third UUID group). Constants: `include/slate_uuids.hpp`.

Alongside: GAP + GATT, **BAS**, **DIS**, Nordic legacy DFU when NimBLE is linked.
HRS GATT deferred (sensor driver). CTS is CONTROL `0x20` time sync, not GATT CTS.
See `docs/ota.md`.

## Framing (§4.2)

Implementation: `include/sdp_frame.hpp`, `src/sdp_frame.cpp`.

- Byte0: `CHAN(3) | FLAGS(5)` — FIRST, LAST, ACK_REQ, URGENT; bit4 reserved=0
- Byte1: per-channel SEQ (wraps)
- Bytes 2–3: total length (u16 LE) only when FIRST and not LAST
- Reassembly buffer 4 KB; malformed / OOO / oversized → drop + channel reset
- Channel 7 (DIAG): debug builds only; release rejects 6 and 7

Host tests: `tests/host/test_sdp_frame.cpp`.

## Loopback / DIAG benchmark

`ble::Link` dispatches completed DIAG (ch7) messages to `sdp::diag::Bench` in debug
builds (`SLATE_BLE_DIAG=1`). Ops cover throughput timing, RTT echo, mbuf high-water,
and parse+render timing — see `docs/benchmark.md`.

Opaque payloads (no known opcode) still echo for M5 bring-up.

## Connection negotiation order

1. ATT MTU 247  
2. DLE (251/251 octets)  
3. 2M PHY — **requested but currently never granted**, see below  
4. Connection interval for session profile (ambient/active/streaming)

STATUS reports the **actual** PHY from `ble_gap_read_le_phy` / PHY-update events
(never hardcoded). Phones often refuse 2M; never assume the request was granted.
The companion requests 2M **after** TX/STATUS CCCD, not during service discovery
— a PHY update (`onPhyUpdate status=6` on nRF52832) must not sit on top of
HELLO_OFFER (`0.8.2-p85`).

STATUS also carries `conn_interval_units` (×1.25 ms). Until m23 / companion
`0.8.2-p87`, that field never reached the phone on ordinary sessions: firmware
only **notified** STATUS from `negotiate_now()` (throughput gates), and the
companion never **read** the characteristic. m23 notifies on STATUS CCCD /
MTU / CONN_UPDATE / PHY and snapshots the interval on connect; p87 reads
STATUS after subscribe (and again ~2 s later after HIGH-priority may settle).

## Companion GATT (central)

`SlateGattClient` / `LinkForegroundService` (companion `0.8.2-p84`+):

| Path | Behaviour |
|---|---|
| Open Main / **Start / reconnect** | `force` abort if a connect is in flight (`disconnect` then `close`); MAC-filtered LE scan; `connectGatt(autoConnect=false)` on the scan result. Does **not** tear down a live GATT session. |
| Boot | `autoConnect=true` (no short timeout). |
| Package replace | Scan then direct connect (watch is usually nearby). |
| Direct `connectGatt(false)` timeout (20 s) | `disconnect`+`close`, then `autoConnect=true`. |
| Watchdog retry | `autoConnect=true`; does not abort an in-flight attempt. |
| HELLO missing after TX subscribe | Rewrite TX CCCD 0→1 so firmware emits `SUBSCRIBE` and `HELLO_OFFER` (`0.8.2-p85`). |

`getRemoteDevice(mac)` + `autoConnect=false` with no recent scan often never
callbacks on Pixel — that hung p83 (`ignored — already connecting` on tap).
Judge link by `onConnectionStateChange`, then session by `CONTROL op=1`.

### Why `BLE_LL_PHY=0`

`ble_ll_ctrl.c` guards the `LL_PHY_UPDATE_IND` transmit case on `BLE_LL_PHY`
alone, but its helper `ble_ll_ctrl_phy_update_ind_instant()` sits inside that
file's `BLE_LL_ROLE_CENTRAL` block. Only a central ever transmits that PDU, so
the case is unreachable for Slate, yet it still fails to link.

The previous workaround was `ROLE_CENTRAL=1`. That is not viable: a central also
needs `ROLE_OBSERVER` for `ble_ll_scan_get_local_rpa()` /
`ble_ll_scan_get_peer_rpa()`, and enabling the scanner costs RAM we do not have.
The combination only ever linked because `ble_ll_init()` was dead code (below),
so the linker stripped the whole path.

Restoring 2M PHY needs the upstream guard widened to
`BLE_LL_PHY && BLE_LL_ROLE_CENTRAL`. Until then Slate runs 1M only.

## Controller init: `ble_ll_init()` is called by hand

`nimble/controller/pkg.yml` registers `ble_ll_init()` as a sysinit entry ordered
`$before` both `ble_transport_hs_init` and `ble_transport_ll_init`. Slate uses
the standalone porting layer, which has no sysinit, and `nimble_port_init()`
does **not** call it — `ble_transport_ll_init()` only sends the HCI no-op.

Without it `g_ble_ll_data.ll_evq.q` stays NULL in BSS and the `ll` task faults in
`xQueueReceive` (`configASSERT`, `queue.c:1513`) the instant the scheduler
starts, which reads as an immediate silent reboot to whatever MCUBoot reverts to.

`ble::start_stack()` therefore calls `ble_ll_init()` itself, right after
`nimble_port_init()`, and asserts both event queues are non-NULL before creating
the tasks. Re-check this whenever the NimBLE submodule is bumped.

Because that call was missing, the whole controller init path was unreachable and
the linker discarded it. Three further defects were hiding behind it, all of
which only surface once the link layer actually runs:

### NPL critical section (`port/nrf52/include/nimble/nimble_npl_os.h`)

Upstream implements `ble_npl_hw_enter_critical()` as `vPortEnterCritical()`, the
*task level* FreeRTOS critical section, which asserts when entered from an
exception. The link layer takes that lock inside the RADIO and TIMER0 ISRs, so
the radio faults immediately (`port.c` VECTACTIVE assert).

Slate shadows the NPL header — `port/nrf52/include` precedes the submodule on the
include path — renames the stock inlines aside with `#include_next`, and replaces
them with a PRIMASK save/restore, matching Mynewt's own NPL. Masking PRIMASK also
blocks SysTick and PendSV, so it still serialises against the scheduler.

### `RADIO_IRQn` priority requires `-DFREERTOS`

`ble_phy.c` picks priority **5** only `#ifdef FREERTOS`, else **0**. Priority 0 is
above `configMAX_SYSCALL_INTERRUPT_PRIORITY` (3), so the radio ISR would trip
`vPortValidateInterruptPriority()` the first time it posts to an event queue.
`cmake/Nimble.cmake` defines `FREERTOS=1` for this reason. Verify with
`objdump -d` on `ble_phy.c.obj`: `ble_phy_init` must store `0xA0` (5 << 5) to
`NVIC->IP[RADIO_IRQn]`.

### os_cputime must be RTC0 at 32768 Hz

`nimble_port_init()` hardcodes `hal_timer_init(5, NULL)` and
`os_cputime_init(32768)`. In NimBLE's nRF52 `hal_timer`, timer **5 is RTC0**.
Slate previously set `TIMER_0=1`, `TIMER_5=0`, `OS_CPUTIME_TIMER_NUM=0`,
`OS_CPUTIME_FREQ=1000000`, which meant `hal_timer_init(5, …)` returned `EINVAL`,
os_cputime had no timer at all, and `hal_timer` was fighting `ble_phy.c` over
TIMER0. `OS_CPUTIME_FREQ` additionally selects the tick↔µs conversions at compile
time, so it must match the frequency the hardware is configured for.

Alongside Slate GATT: **BAS** and **DIS** (HRS deferred). Time sync uses CONTROL
`0x20` until a GATT CTS client exists.

Actual values are logged over RTT and published on STATUS.

## Mbuf pool (6 KB target)

See `include/slate_nimble_cfg.hpp` for the full argument.

| Item | Value |
|---|---|
| `MSYS_1_BLOCK_SIZE` | 292 |
| `MSYS_1_BLOCK_COUNT` | **6** (trimmed from 8 for RAM gate) |
| Est. pool RAM | ≈ 1992 B |
| Host + pool | ≈ 5–7 KB typical |

**Verdict:** pure mbuf pool is fine under 6 KB. Combined host+mbuf at MTU 247+DLE is tight; cut *count* before shrinking block *size* below 292.

### Measured Release (NimBLE ON, BAS/DIS/PHY, `build/dfu-nimble`)

From `slate_firmware.map` / `arm-none-eabi-size` (after boot-heap fix):

| | |
|---|---|
| `.data` + `.bss` (incl. FreeRTOS `ucHeap` **16 KB**) | see size after link |
| Newlib heap reserve | 512 B |
| MSP stack reserve | **4 KB** |
| RAM used (`build/dfu-prod` Release) | **59.5 KB / 64 KB** (4.5 KB left, 4 KB of it the MSP reserve) |

Note: whole-image slack is below the aspirational 6 KB gate after restoring a
bootable FreeRTOS heap (9 KB starved `xTaskCreate` for NimBLE → WDT revert).
Further static trim is required before declaring the RAM gate green again.

MSP is **4 KB** (`SLATE_STACK_SIZE=0x1000`) so nested NimBLE IRQs are not a
confounder. Newlib heap stays minimal (heap_4 owns FreeRTOS allocations).

## TIMER0 / TIMER1

NimBLE’s nRF52 controller claims **TIMER0 + RADIO + RTC0**. Slate’s
`board::micros()` / busy-wait timebase uses **TIMER1 only**
(`src/system_nrf52832.cpp`). Do not reintroduce TIMER0 in app code — that
collision can hard-fault within hundreds of ms after radio start while still
allowing a first frame.

TIMER0 is used by `ble_phy.c` directly, not through `hal_timer`, so
`MYNEWT_VAL_TIMER_0` must stay **0**; RTC0 is `MYNEWT_VAL_TIMER_5=1` and backs
os_cputime. RTC1 is `board::micros()`, RTC2 is power instrumentation.

## Building with NimBLE

`SLATE_HAS_NIMBLE` defaults **ON**. Stacks live in git submodules — not an
external `-DNIMBLE_ROOT=` path:

```powershell
git submodule update --init --recursive
cmake -S . -B build/dfu -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DBOOTLOADER_PRESENT=ON
cmake --build build/dfu
cmake --build build/dfu --target slate_dfu
```

Bring-up without radio: `-DSLATE_HAS_NIMBLE=OFF`.

Vendored trees:

| Path | Role |
|---|---|
| `third_party/mynewt-nimble` | Apache NimBLE host + controller |
| `third_party/FreeRTOS-Kernel` | FreeRTOS kernel |
| `third_party/nrfx` / `third_party/CMSIS_5` | nRF clock/NVMC + CMSIS core |
| `port/nrf52/` | Syscfg overlay, NPL ISR install, silent logs |
| `cmake/Nimble.cmake` | Sources + include paths |

App runtime: one FreeRTOS **app** task owns the former bare-metal main loop
(WDT pet, session tick, local core); NimBLE LL/host tasks run beside it.
Roadmap §3.1 multi-task split (`display` / `link` / `sensors` / `system`) is
**deferred** — not required to close M5a.

### M5a scheduler smoke

Debug builds (`SLATE_BLE_DIAG=1`) run `freertos_smoke::create_tasks()` — a
producer/consumer pair + queue — before the scheduler. They log PASS/FAIL over
RTT, self-delete, and stamp **STATUS byte 11**. Release builds skip smoke so
peak FreeRTOS heap stays available for NimBLE ll/ble + app (a Release smoke +
9 KB heap previously starved task create → WDT → MCUBoot revert).

GAP disconnect calls `session.on_link_down`. Advertising includes the Slate
service UUID.

## Host tests (no hardware)

llvm-mingw EXEs need `libc++.dll` / `libunwind.dll`. Prefer the helper script (sets PATH
and copies runtimes into the build dir via CMake):

```powershell
.\scripts\run_host_tests.cmd
.\scripts\run_host_tests.cmd session
```

If you prefer PowerShell and hit an execution-policy error on the `.ps1`:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_host_tests.ps1
```

Or manually:

```powershell
$env:Path = "<llvm-mingw>\bin;" + $env:Path   # folder that contains libc++.dll
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests
ctest --test-dir build/host-tests -V
```
