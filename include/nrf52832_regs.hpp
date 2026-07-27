#pragma once

#include <cstdint>

namespace nrf {

template <typename T>
inline volatile T& reg(std::uintptr_t address) {
  return *reinterpret_cast<volatile T*>(address);
}

constexpr std::uintptr_t CLOCK_BASE = 0x40000000u;
constexpr std::uintptr_t POWER_BASE = 0x40000000u + 0x00000u;
constexpr std::uintptr_t UICR_BASE = 0x10001000u;
constexpr std::uintptr_t NVMC_BASE = 0x4001E000u;
constexpr std::uintptr_t GPIO0_BASE = 0x50000000u;
constexpr std::uintptr_t TIMER0_BASE = 0x40008000u;

constexpr std::uint32_t NVIC_NUM_VECTORS = 16u + 39u;

namespace power {
constexpr std::uintptr_t DCDCEN = POWER_BASE + 0x578u;
}

namespace clock {
constexpr std::uintptr_t TASKS_LFCLKSTART = CLOCK_BASE + 0x008u;
constexpr std::uintptr_t EVENTS_LFCLKSTARTED = CLOCK_BASE + 0x104u;
constexpr std::uintptr_t LFCLKSRC = CLOCK_BASE + 0x518u;
constexpr std::uint32_t LFCLK_SRC_RC = 0u;
constexpr std::uint32_t LFCLK_SRC_XTAL = 1u;
constexpr std::uint32_t LFCLK_SRC_SYNTH = 2u;
}

namespace nvmc {
constexpr std::uintptr_t READY = NVMC_BASE + 0x400u;
constexpr std::uintptr_t CONFIG = NVMC_BASE + 0x504u;
constexpr std::uint32_t CONFIG_REN = 0u;
constexpr std::uint32_t CONFIG_WEN = 1u;
}

namespace uicr {
constexpr std::uintptr_t NFCPINS = UICR_BASE + 0x20Cu;
constexpr std::uint32_t NFCPINS_RESET_VALUE = 0xFFFFFFFFu;
constexpr std::uint32_t NFCPINS_GPIO = 0xFFFFFFFEu;
}

namespace gpio {
constexpr std::uintptr_t OUT = GPIO0_BASE + 0x504u;
constexpr std::uintptr_t OUTSET = GPIO0_BASE + 0x508u;
constexpr std::uintptr_t OUTCLR = GPIO0_BASE + 0x50Cu;
constexpr std::uintptr_t IN = GPIO0_BASE + 0x510u;
constexpr std::uintptr_t DIR = GPIO0_BASE + 0x514u;
constexpr std::uintptr_t DIRSET = GPIO0_BASE + 0x518u;
constexpr std::uintptr_t DIRCLR = GPIO0_BASE + 0x51Cu;
constexpr std::uintptr_t PIN_CNF_BASE = GPIO0_BASE + 0x700u;

constexpr std::uint32_t PIN_CNF_DIR_INPUT = 0u << 0;
constexpr std::uint32_t PIN_CNF_DIR_OUTPUT = 1u << 0;
constexpr std::uint32_t PIN_CNF_INPUT_CONNECT = 0u << 1;
constexpr std::uint32_t PIN_CNF_INPUT_DISCONNECT = 1u << 1;
constexpr std::uint32_t PIN_CNF_PULL_DISABLED = 0u << 2;
constexpr std::uint32_t PIN_CNF_PULLDOWN = 1u << 2;
constexpr std::uint32_t PIN_CNF_PULLUP = 3u << 2;
constexpr std::uint32_t PIN_CNF_DRIVE_S0S1 = 0u << 8;
constexpr std::uint32_t PIN_CNF_SENSE_DISABLED = 0u << 16;

inline std::uintptr_t pin_cnf(std::uint32_t pin) {
  return PIN_CNF_BASE + (pin * sizeof(std::uint32_t));
}
}  // namespace gpio

namespace timer0 {
constexpr std::uintptr_t TASKS_START = TIMER0_BASE + 0x000u;
constexpr std::uintptr_t TASKS_STOP = TIMER0_BASE + 0x004u;
constexpr std::uintptr_t TASKS_CLEAR = TIMER0_BASE + 0x00Cu;
constexpr std::uintptr_t TASKS_CAPTURE0 = TIMER0_BASE + 0x040u;
constexpr std::uintptr_t MODE = TIMER0_BASE + 0x504u;
constexpr std::uintptr_t BITMODE = TIMER0_BASE + 0x508u;
constexpr std::uintptr_t PRESCALER = TIMER0_BASE + 0x510u;
constexpr std::uintptr_t CC0 = TIMER0_BASE + 0x540u;

constexpr std::uint32_t MODE_TIMER = 0u;
constexpr std::uint32_t BITMODE_16 = 0u;
constexpr std::uint32_t BITMODE_32 = 3u;
}  // namespace timer0

constexpr std::uint32_t PIN_MOTOR = 16u;

// ── SPIM0 (SPI master with EasyDMA) ─────────────────────────────────────────
constexpr std::uintptr_t SPIM0_BASE = 0x40003000u;

namespace spim0 {
constexpr std::uintptr_t TASKS_START      = SPIM0_BASE + 0x010u;
constexpr std::uintptr_t TASKS_STOP       = SPIM0_BASE + 0x014u;
constexpr std::uintptr_t EVENTS_STOPPED   = SPIM0_BASE + 0x104u;
constexpr std::uintptr_t EVENTS_ENDRX     = SPIM0_BASE + 0x110u;
constexpr std::uintptr_t EVENTS_END       = SPIM0_BASE + 0x118u;
constexpr std::uintptr_t EVENTS_ENDTX     = SPIM0_BASE + 0x120u;
constexpr std::uintptr_t EVENTS_STARTED   = SPIM0_BASE + 0x14Cu;
constexpr std::uintptr_t INTENSET         = SPIM0_BASE + 0x304u;
constexpr std::uintptr_t INTENCLR         = SPIM0_BASE + 0x308u;
constexpr std::uintptr_t ENABLE           = SPIM0_BASE + 0x500u;
constexpr std::uintptr_t PSEL_SCK         = SPIM0_BASE + 0x508u;
constexpr std::uintptr_t PSEL_MOSI        = SPIM0_BASE + 0x50Cu;
constexpr std::uintptr_t PSEL_MISO        = SPIM0_BASE + 0x510u;
constexpr std::uintptr_t FREQUENCY        = SPIM0_BASE + 0x524u;
constexpr std::uintptr_t RXD_PTR          = SPIM0_BASE + 0x534u;
constexpr std::uintptr_t RXD_MAXCNT       = SPIM0_BASE + 0x538u;
constexpr std::uintptr_t RXD_AMOUNT       = SPIM0_BASE + 0x53Cu;
constexpr std::uintptr_t TXD_PTR          = SPIM0_BASE + 0x544u;
constexpr std::uintptr_t TXD_MAXCNT       = SPIM0_BASE + 0x548u;
constexpr std::uintptr_t TXD_AMOUNT       = SPIM0_BASE + 0x54Cu;
constexpr std::uintptr_t CONFIG           = SPIM0_BASE + 0x554u;
constexpr std::uintptr_t ORC              = SPIM0_BASE + 0x5C0u;

// FREQUENCY register values (nRF52832 PS v1.8 §35.9)
constexpr std::uint32_t FREQ_125K  = 0x02000000u;
constexpr std::uint32_t FREQ_250K  = 0x04000000u;
constexpr std::uint32_t FREQ_500K  = 0x08000000u;
constexpr std::uint32_t FREQ_1M    = 0x10000000u;
constexpr std::uint32_t FREQ_2M    = 0x20000000u;
constexpr std::uint32_t FREQ_4M    = 0x40000000u;
constexpr std::uint32_t FREQ_8M    = 0x80000000u;

// CONFIG: bit0 = ORDER(0=MSB), bit1 = CPHA, bit2 = CPOL
// SPI mode 3: CPOL=1, CPHA=1 → bits[2:1] = 0b11
constexpr std::uint32_t CONFIG_MODE3_MSB  = (1u << 2) | (1u << 1);

// ENABLE value for SPIM
constexpr std::uint32_t ENABLE_SPIM       = 7u;
constexpr std::uint32_t DISABLE           = 0u;

// PSEL disconnect bit
constexpr std::uint32_t PSEL_DISCONNECTED = (1u << 31);
}  // namespace spim0

// ── TIMER1 — used for render/transmit profiling ──────────────────────────────
constexpr std::uintptr_t TIMER1_BASE = 0x40009000u;

namespace timer1 {
constexpr std::uintptr_t TASKS_START    = TIMER1_BASE + 0x000u;
constexpr std::uintptr_t TASKS_STOP     = TIMER1_BASE + 0x004u;
constexpr std::uintptr_t TASKS_CLEAR    = TIMER1_BASE + 0x00Cu;
constexpr std::uintptr_t TASKS_CAPTURE0 = TIMER1_BASE + 0x040u;
constexpr std::uintptr_t TASKS_CAPTURE1 = TIMER1_BASE + 0x044u;
constexpr std::uintptr_t MODE           = TIMER1_BASE + 0x504u;
constexpr std::uintptr_t BITMODE        = TIMER1_BASE + 0x508u;
constexpr std::uintptr_t PRESCALER      = TIMER1_BASE + 0x510u;
constexpr std::uintptr_t CC0            = TIMER1_BASE + 0x540u;
constexpr std::uintptr_t CC1            = TIMER1_BASE + 0x544u;

constexpr std::uint32_t MODE_TIMER  = 0u;
constexpr std::uint32_t BITMODE_32  = 3u;
}  // namespace timer1

// ── PWM0 — backlight ─────────────────────────────────────────────────────────
constexpr std::uintptr_t PWM0_BASE = 0x4001C000u;

namespace pwm0 {
constexpr std::uintptr_t TASKS_STOP       = PWM0_BASE + 0x004u;
constexpr std::uintptr_t TASKS_SEQSTART0  = PWM0_BASE + 0x008u;
constexpr std::uintptr_t EVENTS_STOPPED   = PWM0_BASE + 0x104u;
constexpr std::uintptr_t ENABLE           = PWM0_BASE + 0x500u;
constexpr std::uintptr_t MODE             = PWM0_BASE + 0x504u;
constexpr std::uintptr_t COUNTERTOP       = PWM0_BASE + 0x508u;
constexpr std::uintptr_t PRESCALER        = PWM0_BASE + 0x50Cu;
constexpr std::uintptr_t DECODER          = PWM0_BASE + 0x510u;
constexpr std::uintptr_t LOOP             = PWM0_BASE + 0x514u;
constexpr std::uintptr_t SEQ0_PTR         = PWM0_BASE + 0x520u;
constexpr std::uintptr_t SEQ0_CNT         = PWM0_BASE + 0x524u;
constexpr std::uintptr_t SEQ0_REFRESH     = PWM0_BASE + 0x528u;
constexpr std::uintptr_t SEQ0_ENDDELAY    = PWM0_BASE + 0x52Cu;
constexpr std::uintptr_t PSEL_OUT0        = PWM0_BASE + 0x560u;
constexpr std::uintptr_t PSEL_OUT1        = PWM0_BASE + 0x564u;
constexpr std::uintptr_t PSEL_OUT2        = PWM0_BASE + 0x568u;
constexpr std::uintptr_t PSEL_OUT3        = PWM0_BASE + 0x56Cu;

constexpr std::uint32_t MODE_UP           = 0u;
constexpr std::uint32_t PRESCALER_DIV1    = 0u;
constexpr std::uint32_t DECODER_LOAD_INDIVIDUAL = 2u;
constexpr std::uint32_t DECODER_MODE_REFRESHCOUNT = 0u;
constexpr std::uint32_t POLARITY_ACTIVE_LOW = (1u << 15);  // bit15 in duty word = invert
}  // namespace pwm0

// ── TWIM1 (I2C master with EasyDMA) ──────────────────────────────────────────
// SPIM0 occupies instance 0 at 0x40003000; touch must use TWIM1 at 0x40004000.
constexpr std::uintptr_t TWIM1_BASE = 0x40004000u;

namespace twim1 {
constexpr std::uintptr_t TASKS_STARTRX    = TWIM1_BASE + 0x000u;
constexpr std::uintptr_t TASKS_STARTTX    = TWIM1_BASE + 0x008u;
constexpr std::uintptr_t TASKS_STOP       = TWIM1_BASE + 0x014u;
constexpr std::uintptr_t TASKS_SUSPEND    = TWIM1_BASE + 0x01Cu;
constexpr std::uintptr_t TASKS_RESUME     = TWIM1_BASE + 0x020u;
constexpr std::uintptr_t EVENTS_STOPPED   = TWIM1_BASE + 0x104u;
constexpr std::uintptr_t EVENTS_ERROR     = TWIM1_BASE + 0x124u;
constexpr std::uintptr_t EVENTS_SUSPENDED = TWIM1_BASE + 0x148u;
constexpr std::uintptr_t EVENTS_RXSTARTED = TWIM1_BASE + 0x14Cu;
constexpr std::uintptr_t EVENTS_TXSTARTED = TWIM1_BASE + 0x150u;
constexpr std::uintptr_t EVENTS_LASTRX    = TWIM1_BASE + 0x15Cu;
constexpr std::uintptr_t EVENTS_LASTTX    = TWIM1_BASE + 0x160u;
constexpr std::uintptr_t SHORTS           = TWIM1_BASE + 0x200u;
constexpr std::uintptr_t INTENSET         = TWIM1_BASE + 0x304u;
constexpr std::uintptr_t INTENCLR         = TWIM1_BASE + 0x308u;
constexpr std::uintptr_t ERRORSRC         = TWIM1_BASE + 0x4C4u;
constexpr std::uintptr_t ENABLE           = TWIM1_BASE + 0x500u;
constexpr std::uintptr_t PSEL_SCL         = TWIM1_BASE + 0x508u;
constexpr std::uintptr_t PSEL_SDA         = TWIM1_BASE + 0x50Cu;
constexpr std::uintptr_t FREQUENCY        = TWIM1_BASE + 0x524u;
constexpr std::uintptr_t RXD_PTR          = TWIM1_BASE + 0x534u;
constexpr std::uintptr_t RXD_MAXCNT       = TWIM1_BASE + 0x538u;
constexpr std::uintptr_t RXD_AMOUNT       = TWIM1_BASE + 0x53Cu;
constexpr std::uintptr_t TXD_PTR          = TWIM1_BASE + 0x544u;
constexpr std::uintptr_t TXD_MAXCNT       = TWIM1_BASE + 0x548u;
constexpr std::uintptr_t TXD_AMOUNT       = TWIM1_BASE + 0x54Cu;
constexpr std::uintptr_t ADDRESS          = TWIM1_BASE + 0x588u;

constexpr std::uint32_t ENABLE_TWIM       = 6u;
constexpr std::uint32_t DISABLE           = 0u;
constexpr std::uint32_t FREQ_100K         = 0x01980000u;
constexpr std::uint32_t FREQ_250K         = 0x04000000u;
constexpr std::uint32_t FREQ_400K         = 0x06400000u;

constexpr std::uint32_t ERRORSRC_OVERRUN  = (1u << 0);
constexpr std::uint32_t ERRORSRC_ANACK    = (1u << 1);
constexpr std::uint32_t ERRORSRC_DNACK    = (1u << 2);

constexpr std::uint32_t SHORT_LASTTX_STOP = (1u << 8);
constexpr std::uint32_t SHORT_LASTTX_SUSPEND = (1u << 7);
constexpr std::uint32_t SHORT_LASTTX_STARTRX = (1u << 10);
constexpr std::uint32_t SHORT_LASTRX_STOP = (1u << 12);
}  // namespace twim1

// ── GPIOTE ───────────────────────────────────────────────────────────────────
constexpr std::uintptr_t GPIOTE_BASE = 0x40006000u;

namespace gpiote {
constexpr std::uintptr_t TASKS_OUT0   = GPIOTE_BASE + 0x000u;
constexpr std::uintptr_t EVENTS_IN0   = GPIOTE_BASE + 0x100u;
constexpr std::uintptr_t EVENTS_IN1   = GPIOTE_BASE + 0x104u;
constexpr std::uintptr_t EVENTS_IN2   = GPIOTE_BASE + 0x108u;
constexpr std::uintptr_t EVENTS_IN3   = GPIOTE_BASE + 0x10Cu;
constexpr std::uintptr_t EVENTS_PORT  = GPIOTE_BASE + 0x17Cu;
constexpr std::uintptr_t INTENSET     = GPIOTE_BASE + 0x304u;
constexpr std::uintptr_t INTENCLR     = GPIOTE_BASE + 0x308u;
constexpr std::uintptr_t CONFIG0      = GPIOTE_BASE + 0x510u;
constexpr std::uintptr_t CONFIG1      = GPIOTE_BASE + 0x514u;
constexpr std::uintptr_t CONFIG2      = GPIOTE_BASE + 0x518u;
constexpr std::uintptr_t CONFIG3      = GPIOTE_BASE + 0x51Cu;

constexpr std::uint32_t CONFIG_MODE_DISABLED = 0u;
constexpr std::uint32_t CONFIG_MODE_EVENT    = 1u;
constexpr std::uint32_t CONFIG_POLARITY_LOTOHI = (1u << 16);
constexpr std::uint32_t CONFIG_POLARITY_HITOLO = (2u << 16);
constexpr std::uint32_t CONFIG_POLARITY_TOGGLE = (3u << 16);

inline std::uintptr_t events_in(std::uint32_t channel) {
  return EVENTS_IN0 + (channel * 4u);
}
inline std::uintptr_t config(std::uint32_t channel) {
  return CONFIG0 + (channel * 4u);
}
}  // namespace gpiote

// ── NVIC (Cortex-M4 SCS) ─────────────────────────────────────────────────────
constexpr std::uintptr_t NVIC_ISER0 = 0xE000E100u;
constexpr std::uintptr_t NVIC_ICER0 = 0xE000E180u;
constexpr std::uintptr_t NVIC_ICPR0 = 0xE000E280u;

namespace irq {
constexpr std::uint32_t GPIOTE = 6u;
constexpr std::uint32_t TWIM1  = 4u;
}  // namespace irq

inline void nvic_enable_irq(std::uint32_t irq_number) {
  reg<std::uint32_t>(NVIC_ISER0) = (1u << irq_number);
}

inline void nvic_disable_irq(std::uint32_t irq_number) {
  reg<std::uint32_t>(NVIC_ICER0) = (1u << irq_number);
}

inline void nvic_clear_pending(std::uint32_t irq_number) {
  reg<std::uint32_t>(NVIC_ICPR0) = (1u << irq_number);
}

}  // namespace nrf
