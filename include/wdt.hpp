#pragma once

// InfiniTime MCUBoot starts the NRF WDT (~7s) before jumping to the app.
// Petting while broken prevents auto-revert (BL degraded-case table).

namespace slate {
namespace wdt {

/** Reload RR0 if the watchdog is running. Safe no-op if not started. */
void pet();

/**
 * InfiniTime-style service: reload WDT only while the side button is up.
 * Holding the button ~7s lets the BL WDT reset the MCU. Also arms the
 * software 8s SYSRESETREQ fallback via board::poll_reboot_button().
 * Call from the app task loop (and from bounded flash helpers), not from
 * tick/idle hooks — those must starve if the app wedges.
 */
void pet_service();

/** True when RUNSTATUS indicates the WDT is active (bootloader path). */
bool is_running();

/**
 * Fatal sealed path: IRQs off, motor off, never pet, poll button for reset.
 * Lets MCUBoot revert unconfirmed images; confirmed images still reboot.
 */
[[noreturn]] void fatal_starve();

}  // namespace wdt
}  // namespace slate
