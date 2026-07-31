#pragma once

#include <cstdint>

namespace board {

constexpr std::uint32_t kMotorPin = 16u;  // active-low haptic (high = off)
// InfiniTime leaves ButtonEnable high for the life of the app (~34 µA) and reads
// Button with a pulldown (pressed = HIGH). Same contract here so WDT withhold and
// long-press reset work even when higher-level tasks are wedged.
constexpr std::uint32_t kButtonEnablePin = 15u;
constexpr std::uint32_t kButtonSensePin  = 13u;

void busy_wait_us(std::uint32_t microseconds);
void busy_wait_ms(std::uint32_t milliseconds);
void motor_on();
void motor_off();
void pulse_motor(std::uint32_t milliseconds);

// Microsecond timestamp from TIMER1 (1 MHz free-running). TIMER0 is NimBLE's.
std::uint32_t micros();

/** AIRCR SYSRESETREQ — same as InfiniTime long-press reboot. */
[[noreturn]] void system_reset();

/**
 * POWER->RESETREAS latched at boot, before it is cleared.
 *
 * The register accumulates across resets, so it is captured and zeroed once in
 * capture_reset_reason() and read back from here. Bits (nRF52832 PS v1.4):
 * 0 RESETPIN, 1 DOG (watchdog), 2 SREQ (SYSRESETREQ), 3 LOCKUP, 16 OFF,
 * 17 LPCOMP, 18 DIF (debug), 19 NFC, 20 VBUS. All-zero means a power-on or
 * brownout reset, which latches nothing.
 */
std::uint32_t reset_reason();

/** Latch and clear POWER->RESETREAS. Call once, as early as possible. */
void capture_reset_reason();

/** Configure enable high + sense pulldown. Call once early (before WDT pets). */
void button_hw_init();

/** Raw side-button read (enable already high). True = pressed. ISR-safe. */
bool button_raw();

/** Call from app/idle. Continuous press ≥8 s → system_reset() (soft path).
 *  Hung-app recovery is InfiniTime-style: withhold WDT pets while held. */
void poll_reboot_button();

}  // namespace board
