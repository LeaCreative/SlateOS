# I2C / TWI bus rules (normative)

Touch (CST816S), accel (BMA421/425), and heart-rate (HRS3300) share **one**
physical I2C bus on the PineTime: **SDA=P0.06, SCL=P0.07**, mastered by
**TWIM1**. There is no second controller and no private bus per sensor.

These rules exist because breaking them produces symptoms that look like
unrelated product bugs (dead raise-to-wake, HR LED on with `--` BPM, flaky
touch). Normative for firmware changes that touch `twi`, sensor drivers, or
any new FreeRTOS task that talks I2C.

Companion code is out of scope. SPI has its own mutex in `spi_bus.cpp` — same
idea, different peripheral.

---

## Hard rules

### 1. Only `twi::` may drive TWIM1

- All transfers go through `twi::write` / `twi::read` / `twi::write_read`.
- Do **not** poke `TWIM1` registers, SHORTS, ENABLE, or PSEL from drivers,
  power helpers, or tasks. Exception: the implementation inside `src/twi.cpp`
  itself (and its `recover_bus` path).
- Do **not** call `twi::init()` from poll paths to “fix” the bus. ENABLE=0
  preserves PSEL / FREQUENCY / PIN_CNF; transfers already wake and sleep the
  peripheral (InfiniTime `TwiMaster` pattern).

### 2. The bus mutex is mandatory under FreeRTOS

- FreeRTOS builds (`SLATE_HAS_FREERTOS=1`) take a mutex around every transfer
  **and** around `twi::sleep()` / `twi::wake()` / `twi::recover_bus()`.
- That matches InfiniTime `TwiMaster` (`xSemaphoreTake` / `Give` on every
  public Read/Write). SPI already does the same for SPIM0.
- **Never** open-code a second TWIM transfer path that skips the mutex.
- Nested take from the same task deadlocks — do not call `write`/`read` from
  inside a path that already holds the lock (helpers that need both must use
  unlocked internals inside `twi.cpp` only).

### 3. Any new I2C client assumes multi-task contention

Today at least:

| Task | Typical I2C use |
|------|-----------------|
| `app` | Touch poll, BMA steps / raise-to-wake, occasional HRS setup |
| `hr` | HRS3300 sample every ~100 ms while HR is enabled |

Adding a third task that uses I2C is allowed **only** if it still goes through
`twi::`. Do not “optimise” by sampling without the mutex “because we only run
while asleep”. Raise-to-wake and continuous HR both run while the panel is
asleep — that is exactly when races used to corrupt both.

### 4. Timeouts and recovery are not optional

- Every transfer has a bounded timeout (defaults in `twi.hpp`).
- On timeout or ERROR, `twi.cpp` recovers (bit-bang SCL, STOP, re-enable).
- Callers must treat non-`Ok` as failure and **not** invent infinite retries
  on the app task (WDT risk). Prefer skip-this-sample / mark-diag.

### 5. CST816S silence is normal

When idle, the touch controller may NACK or time out. That is not a bus
fault by itself. Do not respond by calling `twi::init()`, disabling the
mutex, or widening timeouts into seconds.

### 6. Power must not yank ENABLE under an in-flight transfer

- `power::buses_idle()` / `twi::sleep()` must take the same mutex so they
  cannot clear `TWIM1.ENABLE` mid-byte while `hr` or `app` is transferring.
- Prefer: sleep only when idle; transfers re-enable on entry.

### 7. Register constants are sacred

- TWIM SHORTS bit positions come from the nRF52832 Product Spec /
  `nrf52_bitfields.h`. They were wrong once (N-31) and killed **every** I2C
  device on every watch. Do not “simplify” or renumber SHORTS by hand.
- Pin drive for SDA/SCL is open-drain **S0D1** with external pull-ups
  (InfiniTime). Do not switch back to S0S1.

### 8. Device addresses (authoritative)

| Device   | 7-bit addr | Notes |
|----------|------------|--------|
| CST816S  | `0x15`     | May sleep on bus |
| BMA421/425 | `0x18`   | Detect variant at runtime |
| HRS3300  | `0x44`     | Sleep via PDRIVER `0x00` when unused |

---

## Checklist before merging I2C-related work

1. New sensor or task? Still only `twi::` APIs?
2. FreeRTOS path still mutex-guarded (no `#ifdef` that strips the lock on
   device builds)?
3. No `twi::init()` in a poll / IRQ / hot path?
4. Failures have timeouts; no unbounded spin?
5. On-device smoke if HR or a new I2C task is involved:
   - With **HR Off**: raise-to-wake still works after display sleep.
   - With **HR On**: LED may light; BPM can lock after several seconds
     on-wrist; raise-to-wake still works.
6. Diag: touch group / `twi::last_status()` still meaningful if reads fail.

---

## Failure modes (what it looks like)

| Symptom | Likely I2C cause |
|---------|------------------|
| Raise broken only when HR On | Mutex missing; BMA reads racing HR |
| HR LED on, face shows `--` forever | Corrupt / zero PPG samples from races or NACK |
| Touch IRQ count rises, reads fail | SHORTS wrong, or bus wedged without recovery |
| Everything I2C dead after bring-up | SHORTS / pin drive / ENABLE misuse |

Historical anchors: N-31 (SHORTS), InfiniTime TwiMaster mutex, 10 Aug 2026
HR-vs-raise race (mutex added to `twi.cpp`).

---

## Code map

| Piece | Path |
|-------|------|
| Bus API + mutex + recovery | `include/twi.hpp`, `src/twi.cpp` |
| Touch | `src/cst816s.cpp` |
| Accel | `src/bma42x.cpp` |
| HR sensor | `src/hrs3300.cpp` |
| HR FreeRTOS task | `src/hr_task.cpp` |
| Raise-to-wake (uses BMA via core) | `src/local_core.cpp` `poll_raise` |
| Idle disable | `src/power.cpp` → `twi::sleep()` |

Parity notes (mechanism differences vs InfiniTime): `docs/infinitime-parity.md`
§ TWI. Prefer their **locking and Sleep/Wakeup** behaviour even when SHORTS
vs manual STOP differs. Agent shorthand: `.cursor/rules/i2c-bus.mdc`.
