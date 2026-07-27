#include "backlight.hpp"
#include "nrf52832_regs.hpp"

#include <algorithm>
#include <cstdint>

// PineTime backlight pins (authoritative — see CLAUDE.md hardware table).
// All active-low.
static constexpr std::uint32_t kPinLow  = 14u;
static constexpr std::uint32_t kPinMid  = 22u;
static constexpr std::uint32_t kPinHigh = 23u;

// PWM counter top — gives 8-bit effective resolution at 16 MHz / 1 / 256 ≈ 62.5 kHz.
// Imperceptible flicker well above 20 kHz.
static constexpr std::uint16_t kCounterTop = 255u;

// Sequence buffer: four 16-bit compare words (one per channel).
// Bit15 = polarity invert (1 → active low, i.e. LED on when counter < compare).
// We use channels 0, 1, 2 for LOW, MID, HIGH respectively.
alignas(4) static std::uint16_t g_seq[4] = {0u, 0u, 0u, 0u};

namespace {

void gpio_output_pwm(std::uint32_t pin) {
    nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
        nrf::gpio::PIN_CNF_DIR_OUTPUT |
        nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
        nrf::gpio::PIN_CNF_PULL_DISABLED |
        nrf::gpio::PIN_CNF_DRIVE_S0S1 |
        nrf::gpio::PIN_CNF_SENSE_DISABLED;
    // Drive high initially (active-low → LED off).
    nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << pin);
}

// Restart the PWM sequence so the new compare values take effect immediately.
void pwm_reload() {
    nrf::reg<std::uint32_t>(nrf::pwm0::SEQ0_PTR)      =
        reinterpret_cast<std::uint32_t>(g_seq);
    nrf::reg<std::uint32_t>(nrf::pwm0::SEQ0_CNT)      = 4u;
    nrf::reg<std::uint32_t>(nrf::pwm0::SEQ0_REFRESH)  = 0u;
    nrf::reg<std::uint32_t>(nrf::pwm0::SEQ0_ENDDELAY) = 0u;
    // Clear SEQSTART event and kick.
    nrf::reg<std::uint32_t>(nrf::pwm0::TASKS_SEQSTART0) = 1u;
}

}  // namespace

namespace backlight {

void init() {
    gpio_output_pwm(kPinLow);
    gpio_output_pwm(kPinMid);
    gpio_output_pwm(kPinHigh);

    // Stop PWM if it was running.
    nrf::reg<std::uint32_t>(nrf::pwm0::ENABLE) = 0u;

    // Connect output pins.
    nrf::reg<std::uint32_t>(nrf::pwm0::PSEL_OUT0) = kPinLow;
    nrf::reg<std::uint32_t>(nrf::pwm0::PSEL_OUT1) = kPinMid;
    nrf::reg<std::uint32_t>(nrf::pwm0::PSEL_OUT2) = kPinHigh;
    nrf::reg<std::uint32_t>(nrf::pwm0::PSEL_OUT3) = nrf::spim0::PSEL_DISCONNECTED;

    // Up-count mode, 16 MHz base clock (PRESCALER=0 → no division).
    nrf::reg<std::uint32_t>(nrf::pwm0::MODE)       = nrf::pwm0::MODE_UP;
    nrf::reg<std::uint32_t>(nrf::pwm0::PRESCALER)  = nrf::pwm0::PRESCALER_DIV1;
    nrf::reg<std::uint32_t>(nrf::pwm0::COUNTERTOP) = kCounterTop;

    // DECODER: LOAD=INDIVIDUAL (each channel has its own word in the sequence).
    nrf::reg<std::uint32_t>(nrf::pwm0::DECODER) =
        (nrf::pwm0::DECODER_LOAD_INDIVIDUAL << 0u) |
        (nrf::pwm0::DECODER_MODE_REFRESHCOUNT << 8u);

    // Infinite loop (LOOP=0 → loop forever until TASKS_STOP).
    nrf::reg<std::uint32_t>(nrf::pwm0::LOOP) = 0u;

    // Enable PWM peripheral.
    nrf::reg<std::uint32_t>(nrf::pwm0::ENABLE) = 1u;

    // Start with brightness 0 (all LEDs off).
    set(0u);
}

void set(std::uint32_t percent) {
    percent = std::min(percent, 100u);

    // Map 0-100% to a compare value 0-kCounterTop.
    // Duty cycle of `compare/kCounterTop` of the period is spent low (LED on)
    // when polarity-invert bit is set.  At 0% all LEDs off; at 100% all full on.
    const std::uint16_t duty = static_cast<std::uint16_t>(
        (percent * kCounterTop) / 100u);

    // Bit15=1 → polarity inverted (active-low) so LED is ON when counter < duty.
    const std::uint16_t word = static_cast<std::uint16_t>(
        nrf::pwm0::POLARITY_ACTIVE_LOW | duty);

    if (percent == 0u) {
        // Full off: set duty to 0 — counter never < 0, so output stays high.
        g_seq[0] = nrf::pwm0::POLARITY_ACTIVE_LOW | 0u;
        g_seq[1] = nrf::pwm0::POLARITY_ACTIVE_LOW | 0u;
        g_seq[2] = nrf::pwm0::POLARITY_ACTIVE_LOW | 0u;
    } else {
        g_seq[0] = word;
        g_seq[1] = word;
        g_seq[2] = word;
    }
    g_seq[3] = nrf::pwm0::POLARITY_ACTIVE_LOW | 0u;  // unused channel 3

    pwm_reload();
}

void off()      { set(0u);   }
void on_low()   { set(25u);  }
void on_mid()   { set(60u);  }
void on_high()  { set(100u); }

}  // namespace backlight
