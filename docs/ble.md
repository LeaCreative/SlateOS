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

Alongside: Battery (BAS), Device Information (DIS), Heart Rate (HRS — stub until sensors task wires it), Current Time Service as **client**. Nordic legacy DFU remains the M15 recovery path (not registered here).

## Framing (§4.2)

Implementation: `include/sdp_frame.hpp`, `src/sdp_frame.cpp`.

- Byte0: `CHAN(3) | FLAGS(5)` — FIRST, LAST, ACK_REQ, URGENT; bit4 reserved=0
- Byte1: per-channel SEQ (wraps)
- Bytes 2–3: total length (u16 LE) only when FIRST and not LAST
- Reassembly buffer 4 KB; malformed / OOO / oversized → drop + channel reset
- Channel 7 (DIAG): debug builds only; release rejects 6 and 7

Host tests: `tests/host/test_sdp_frame.cpp`.

## Loopback

`ble::Link` echoes completed DIAG (ch7) messages back on TX. Used by M5 bring-up and later measurement gates.

## Connection negotiation order

1. ATT MTU 247  
2. DLE (251/251 octets)  
3. 2M PHY  
4. Connection interval for session profile (ambient/active/streaming)

Actual values are logged over RTT and published on STATUS. Phones often refuse; never assume the request was granted.

## Mbuf pool (6 KB target)

See `include/slate_nimble_cfg.hpp` for the full argument.

| Item | Value |
|---|---|
| `MSYS_1_BLOCK_SIZE` | 292 |
| `MSYS_1_BLOCK_COUNT` | 8 |
| Est. pool RAM | ≈ 2656 B |
| Host + pool | ≈ 5.7–7.2 KB typical |

**Verdict:** pure mbuf pool is fine under 6 KB. Combined host+mbuf at MTU 247+DLE is tight; **7–8 KB** is the realistic measured floor once BAS/DIS/bonding are on. Shrinking block *size* below 292 while advertising MTU 247 is wrong; cut *count* to 6 only if CI forces the envelope.

Measured usage: report from `.map` / `ble_npl_hw_get_mem` after linking NimBLE (`SLATE_HAS_NIMBLE=1`). Until the radio stack is linked, host tests validate framing/link only.

## Building with NimBLE

Default firmware build is transport-ready with `SLATE_HAS_NIMBLE=0` (stubs log UUID and idle the radio). To link Apache NimBLE + FreeRTOS:

```powershell
cmake -S . -B build/debug -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Debug `
  -DSLATE_HAS_NIMBLE=1 `
  -DNIMBLE_ROOT=C:/path/to/mynewt-nimble `
  -DFREERTOS_ROOT=C:/path/to/FreeRTOS-Kernel
```

Port files live under `src/ble_nimble.cpp` and `config/FreeRTOSConfig.h`. Syscfg must set `MSYS_1_BLOCK_*` to the Slate values above.

## Host tests (no hardware)

```powershell
cmake -S tests/host -B build/host-tests
cmake --build build/host-tests
ctest --test-dir build/host-tests -V
```
