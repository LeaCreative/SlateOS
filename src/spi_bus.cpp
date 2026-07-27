#include "spi_bus.hpp"
#include "nrf52832_regs.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

// PineTime SPI bus pins (authoritative — see CLAUDE.md / hardware table)
static constexpr std::uint32_t kPinSck  = 2u;
static constexpr std::uint32_t kPinMosi = 3u;
static constexpr std::uint32_t kPinMiso = 4u;
static constexpr std::uint32_t kPinDc   = 18u;  // ST7789 DC/RS
static constexpr std::uint32_t kPinCsDisplay = 25u;
static constexpr std::uint32_t kPinCsFlash   = 5u;

namespace {

bool g_acquired = false;

void gpio_output(std::uint32_t pin, bool initial_high) {
    nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
        nrf::gpio::PIN_CNF_DIR_OUTPUT |
        nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
        nrf::gpio::PIN_CNF_PULL_DISABLED |
        nrf::gpio::PIN_CNF_DRIVE_S0S1 |
        nrf::gpio::PIN_CNF_SENSE_DISABLED;

    if (initial_high) {
        nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << pin);
    } else {
        nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << pin);
    }
}

}  // namespace

namespace spi {

void init() {
    // Ensure SPIM0 is disabled before reconfiguring.
    nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::DISABLE;

    // Configure SCK/MOSI outputs; MISO input for XT25 reads.
    gpio_output(kPinSck,  false);
    gpio_output(kPinMosi, false);
    nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(kPinMiso)) =
        nrf::gpio::PIN_CNF_DIR_INPUT | nrf::gpio::PIN_CNF_INPUT_CONNECT |
        nrf::gpio::PIN_CNF_PULL_DISABLED | nrf::gpio::PIN_CNF_DRIVE_S0S1 |
        nrf::gpio::PIN_CNF_SENSE_DISABLED;

    // DC/RS and CS pins — deasserted (CS high, DC high = data mode is safe default).
    gpio_output(kPinDc,        true);
    gpio_output(kPinCsDisplay, true);
    gpio_output(kPinCsFlash,   true);

    // Wire SPIM0 pin selects.
    nrf::reg<std::uint32_t>(nrf::spim0::PSEL_SCK)  = kPinSck;
    nrf::reg<std::uint32_t>(nrf::spim0::PSEL_MOSI) = kPinMosi;
    nrf::reg<std::uint32_t>(nrf::spim0::PSEL_MISO) = kPinMiso;

    // 8 MHz, SPI mode 3 (CPOL=1, CPHA=1), MSB first.
    nrf::reg<std::uint32_t>(nrf::spim0::FREQUENCY) = nrf::spim0::FREQ_8M;
    nrf::reg<std::uint32_t>(nrf::spim0::CONFIG)    = nrf::spim0::CONFIG_MODE3_MSB;

    // ORC byte sent when TX runs dry (should not happen in our usage, but set 0xFF).
    nrf::reg<std::uint32_t>(nrf::spim0::ORC) = 0xFFu;

    // Enable SPIM0.
    nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::ENABLE_SPIM;

    g_acquired = false;
}

void acquire() {
    // In bare-metal single-threaded context this is a simple flag check.
    assert(!g_acquired && "spi::acquire called while bus already held");
    g_acquired = true;
}

void release() {
    assert(g_acquired && "spi::release called without prior acquire");
    g_acquired = false;
}

bool is_acquired() {
    return g_acquired;
}

void transmit(const std::uint8_t* data, std::size_t len) {
    assert(g_acquired && "spi::transmit called without holding the bus");

    // EasyDMA requires the source buffer to be in RAM (not flash).
    // Callers are responsible for this — tile buffers are always in RAM.

    while (len > 0u) {
        const std::size_t chunk = (len > 0xFFFFu) ? 0xFFFFu : len;

        // Clear END event.
        nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) = 0u;

        // Set up EasyDMA.
        nrf::reg<std::uint32_t>(nrf::spim0::TXD_PTR)    = reinterpret_cast<std::uint32_t>(data);
        nrf::reg<std::uint32_t>(nrf::spim0::TXD_MAXCNT) = static_cast<std::uint32_t>(chunk);
        nrf::reg<std::uint32_t>(nrf::spim0::RXD_MAXCNT) = 0u;

        // Kick the DMA transfer.
        nrf::reg<std::uint32_t>(nrf::spim0::TASKS_START) = 1u;

        // Wait for completion (poll in bare-metal M0; IRQ-driven later with FreeRTOS).
        while (nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) == 0u) {
        }

        data += chunk;
        len  -= chunk;
    }
}

void transfer(const std::uint8_t* tx, std::size_t tx_len,
              std::uint8_t* rx, std::size_t rx_len) {
    assert(g_acquired && "spi::transfer called without holding the bus");
    // Phase 1: command / address (TX only).
    if (tx != nullptr && tx_len > 0u) {
        transmit(tx, tx_len);
    }
    // Phase 2: clock in response. SPIM needs a TX source for clocks — use ORC
    // by setting TXD_MAXCNT to match RX with a dummy RAM byte repeatedly, or
    // one-shot with a zeroed scratch. Chunk into ≤255 with a static 0x00 pad.
    if (rx == nullptr || rx_len == 0u) {
        return;
    }
    // EasyDMA walks TX sequentially — zero pad buffer for clocking.
    alignas(4) static std::uint8_t zeros[256] = {};
    while (rx_len > 0u) {
        const std::size_t chunk = (rx_len > sizeof(zeros)) ? sizeof(zeros) : rx_len;
        nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) = 0u;
        nrf::reg<std::uint32_t>(nrf::spim0::TXD_PTR) =
            reinterpret_cast<std::uint32_t>(zeros);
        nrf::reg<std::uint32_t>(nrf::spim0::TXD_MAXCNT) =
            static_cast<std::uint32_t>(chunk);
        nrf::reg<std::uint32_t>(nrf::spim0::RXD_PTR) =
            reinterpret_cast<std::uint32_t>(rx);
        nrf::reg<std::uint32_t>(nrf::spim0::RXD_MAXCNT) =
            static_cast<std::uint32_t>(chunk);
        nrf::reg<std::uint32_t>(nrf::spim0::TASKS_START) = 1u;
        while (nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) == 0u) {
        }
        rx += chunk;
        rx_len -= chunk;
    }
}

void cs_assert(std::uint32_t pin) {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << pin);
}

void cs_deassert(std::uint32_t pin) {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << pin);
}

void dc_command() {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << kPinDc);
}

void dc_data() {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kPinDc);
}

}  // namespace spi
