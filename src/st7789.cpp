#include "st7789.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "spi_bus.hpp"

#include <cstdint>

// PineTime ST7789 pin assignments (authoritative — see CLAUDE.md)
static constexpr std::uint32_t kPinCs  = 25u;
static constexpr std::uint32_t kPinRst = 26u;

// ST7789 GRAM is 240×320. PineTime / InfiniTime address the visible 240×240 at
// y=0 with Vertical Scroll Start = 0 (no +80 rowstart — that left the top of
// the panel showing leftover InfiniTime pixels while fills painted below).
static constexpr std::uint16_t kColOffset = 0u;
static constexpr std::uint16_t kRowOffset = 0u;
static constexpr std::uint16_t kGramHeight = 320u;

// ── ST7789 command bytes ──────────────────────────────────────────────────────
static constexpr std::uint8_t CMD_NOP      = 0x00u;
static constexpr std::uint8_t CMD_SWRESET  = 0x01u;
static constexpr std::uint8_t CMD_SLPIN    = 0x10u;
static constexpr std::uint8_t CMD_SLPOUT   = 0x11u;
static constexpr std::uint8_t CMD_NORON    = 0x13u;
static constexpr std::uint8_t CMD_INVON    = 0x21u;
static constexpr std::uint8_t CMD_DISPON   = 0x29u;
static constexpr std::uint8_t CMD_CASET    = 0x2Au;
static constexpr std::uint8_t CMD_RASET    = 0x2Bu;
static constexpr std::uint8_t CMD_RAMWR    = 0x2Cu;
static constexpr std::uint8_t CMD_VSCRDEF  = 0x33u;
static constexpr std::uint8_t CMD_MADCTL   = 0x36u;
static constexpr std::uint8_t CMD_VSCSAD   = 0x37u;
static constexpr std::uint8_t CMD_COLMOD   = 0x3Au;
static constexpr std::uint8_t CMD_PORCTRL  = 0xB2u;
static constexpr std::uint8_t CMD_GCTRL    = 0xB7u;
static constexpr std::uint8_t CMD_VCOMS    = 0xBBu;
static constexpr std::uint8_t CMD_LCMCTRL  = 0xC0u;
static constexpr std::uint8_t CMD_VDVVRHEN = 0xC2u;
static constexpr std::uint8_t CMD_VRHS     = 0xC3u;
static constexpr std::uint8_t CMD_VDVS     = 0xC4u;
static constexpr std::uint8_t CMD_FRCTRL2  = 0xC6u;
static constexpr std::uint8_t CMD_PWCTRL1  = 0xD0u;
static constexpr std::uint8_t CMD_PVGAMCTRL = 0xE0u;
static constexpr std::uint8_t CMD_NVGAMCTRL = 0xE1u;

namespace {

// Send one command byte with CS asserted, DC low (command phase).
bool send_command(std::uint8_t cmd) {
    spi::acquire();
    spi::cs_assert(kPinCs);
    spi::dc_command();
    const bool ok = spi::transmit(&cmd, 1u);
    spi::cs_deassert(kPinCs);
    spi::release();
    return ok;
}

// Send a command byte followed immediately by data bytes in the same CS cycle.
bool send_command_data(std::uint8_t cmd, const std::uint8_t* data,
                       std::uint32_t len) {
    spi::acquire();
    spi::cs_assert(kPinCs);

    spi::dc_command();
    bool ok = spi::transmit(&cmd, 1u);

    if (ok && len > 0u) {
        spi::dc_data();
        ok = spi::transmit(data, len);
    }

    spi::cs_deassert(kPinCs);
    spi::release();
    return ok;
}

void gpio_output_init(std::uint32_t pin, bool initial_high) {
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

void hard_reset() {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << kPinRst);
    board::busy_wait_ms(10u);
    nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kPinRst);
    board::busy_wait_ms(120u);  // ST7789 requires ≥5ms after RST high; 120ms is safe
}

// ST7789 initialisation sequence derived from the Sitronix ST7789 datasheet and
// verified against PineTime community bring-up logs.
void init_sequence() {
    // Software reset — reinitialises all registers to default.
    send_command(CMD_SWRESET);
    board::busy_wait_ms(150u);

    // Sleep out — required before any further commands that affect the display.
    send_command(CMD_SLPOUT);
    board::busy_wait_ms(10u);

    // COLMOD: 16-bit colour (RGB565), 0x55 = 16bpp MCU & RGB interface.
    {
        std::uint8_t d[] = {0x55u};
        send_command_data(CMD_COLMOD, d, sizeof(d));
    }

    // MADCTL: MX mirror + RGB order → correct orientation for PineTime.
    // Bit6=MX(mirror col), Bit5=MY(mirror row)=0, Bit4=MV=0, Bit3=ML=0, Bit2=RGB=0.
    // PineTime production firmware uses 0x00; set 0x00 (no mirror, RGB) here.
    {
        std::uint8_t d[] = {0x00u};
        send_command_data(CMD_MADCTL, d, sizeof(d));
    }

    // PORCTRL: porch control defaults (back/front porch).
    {
        std::uint8_t d[] = {0x0Cu, 0x0Cu, 0x00u, 0x33u, 0x33u};
        send_command_data(CMD_PORCTRL, d, sizeof(d));
    }

    // GCTRL: gate control — VGH=13.26V, VGL=-10.43V.
    {
        std::uint8_t d[] = {0x35u};
        send_command_data(CMD_GCTRL, d, sizeof(d));
    }

    // VCOMS: VCOM setting 0.725V.
    {
        std::uint8_t d[] = {0x19u};
        send_command_data(CMD_VCOMS, d, sizeof(d));
    }

    // LCMCTRL: LCM control.
    {
        std::uint8_t d[] = {0x2Cu};
        send_command_data(CMD_LCMCTRL, d, sizeof(d));
    }

    // VDVVRHEN: VDV and VRH command enable.
    {
        std::uint8_t d[] = {0x01u};
        send_command_data(CMD_VDVVRHEN, d, sizeof(d));
    }

    // VRHS: VRH = 4.45V + (vcom + vcom offset + vdv).
    {
        std::uint8_t d[] = {0x12u};
        send_command_data(CMD_VRHS, d, sizeof(d));
    }

    // VDVS: VDV = 0V.
    {
        std::uint8_t d[] = {0x20u};
        send_command_data(CMD_VDVS, d, sizeof(d));
    }

    // FRCTRL2: frame rate = 60Hz in normal mode.
    {
        std::uint8_t d[] = {0x0Fu};
        send_command_data(CMD_FRCTRL2, d, sizeof(d));
    }

    // PWCTRL1: power control — AVDD=6.8V, AVCL=-4.8V, VDDS=2.3V.
    {
        std::uint8_t d[] = {0xA4u, 0xA1u};
        send_command_data(CMD_PWCTRL1, d, sizeof(d));
    }

    // Positive gamma correction.
    {
        std::uint8_t d[] = {
            0xD0u, 0x04u, 0x0Du, 0x11u, 0x13u, 0x2Bu, 0x3Fu, 0x54u,
            0x4Cu, 0x18u, 0x0Du, 0x0Bu, 0x1Fu, 0x23u
        };
        send_command_data(CMD_PVGAMCTRL, d, sizeof(d));
    }

    // Negative gamma correction.
    {
        std::uint8_t d[] = {
            0xD0u, 0x04u, 0x0Cu, 0x11u, 0x13u, 0x2Cu, 0x3Fu, 0x44u,
            0x51u, 0x2Fu, 0x1Fu, 0x1Fu, 0x20u, 0x23u
        };
        send_command_data(CMD_NVGAMCTRL, d, sizeof(d));
    }

    // Invert display — required for PineTime (IPS panel shows correct colours inverted).
    send_command(CMD_INVON);
    board::busy_wait_ms(1u);

    // Normal display mode on.
    send_command(CMD_NORON);
    board::busy_wait_ms(1u);

    // Reset scroll so a previous firmware’s VSCSAD cannot leave a partial view.
    // TFA=0, VSA=320, BFA=0 — full GRAM; start address 0.
    {
        std::uint8_t d[] = {0x00u, 0x00u, 0x01u, 0x40u, 0x00u, 0x00u};
        send_command_data(CMD_VSCRDEF, d, sizeof(d));
    }
    {
        std::uint8_t d[] = {0x00u, 0x00u};
        send_command_data(CMD_VSCSAD, d, sizeof(d));
    }

    // Display on.
    send_command(CMD_DISPON);
    board::busy_wait_ms(20u);
}

void set_window_abs(std::uint16_t x0, std::uint16_t y0,
                    std::uint16_t x1, std::uint16_t y1) {
    {
        std::uint8_t d[4] = {
            static_cast<std::uint8_t>(x0 >> 8), static_cast<std::uint8_t>(x0),
            static_cast<std::uint8_t>(x1 >> 8), static_cast<std::uint8_t>(x1)
        };
        send_command_data(CMD_CASET, d, sizeof(d));
    }
    {
        std::uint8_t d[4] = {
            static_cast<std::uint8_t>(y0 >> 8), static_cast<std::uint8_t>(y0),
            static_cast<std::uint8_t>(y1 >> 8), static_cast<std::uint8_t>(y1)
        };
        send_command_data(CMD_RASET, d, sizeof(d));
    }
    send_command(CMD_RAMWR);
}

void clear_full_gram(std::uint16_t rgb565) {
    alignas(4) std::uint8_t line[240 * 2];
    const std::uint8_t hi = static_cast<std::uint8_t>(rgb565 >> 8);
    const std::uint8_t lo = static_cast<std::uint8_t>(rgb565 & 0xFFu);
    for (std::uint16_t x = 0; x < 240u; ++x) {
        line[x * 2u] = hi;
        line[x * 2u + 1u] = lo;
    }
    for (std::uint16_t y = 0; y < kGramHeight; ++y) {
        set_window_abs(0, y, 239, y);
        spi::acquire();
        spi::cs_assert(kPinCs);
        spi::dc_data();
        (void)spi::transmit(line, sizeof(line));
        spi::cs_deassert(kPinCs);
        spi::release();
    }
}

}  // namespace

namespace st7789 {

void init() {
    gpio_output_init(kPinCs,  true);   // CS deasserted
    gpio_output_init(kPinRst, true);   // RST deasserted

    hard_reset();
    init_sequence();
    // Wipe all 320 GRAM rows so InfiniTime / recovery pixels cannot remain.
    clear_full_gram(0x0000u);
}

void sleep_in() {
    send_command(CMD_SLPIN);
    board::busy_wait_ms(5u);
}

void sleep_out() {
    send_command(CMD_SLPOUT);
    board::busy_wait_ms(120u);
}

void set_window(std::uint16_t x0, std::uint16_t y0,
                std::uint16_t x1, std::uint16_t y1) {
    const std::uint16_t gx0 = static_cast<std::uint16_t>(x0 + kColOffset);
    const std::uint16_t gx1 = static_cast<std::uint16_t>(x1 + kColOffset);
    const std::uint16_t gy0 = static_cast<std::uint16_t>(y0 + kRowOffset);
    const std::uint16_t gy1 = static_cast<std::uint16_t>(y1 + kRowOffset);

    // CASET — column address (x)
    {
        std::uint8_t d[4] = {
            static_cast<std::uint8_t>(gx0 >> 8), static_cast<std::uint8_t>(gx0),
            static_cast<std::uint8_t>(gx1 >> 8), static_cast<std::uint8_t>(gx1)
        };
        send_command_data(CMD_CASET, d, sizeof(d));
    }

    // RASET — row address (y)
    {
        std::uint8_t d[4] = {
            static_cast<std::uint8_t>(gy0 >> 8), static_cast<std::uint8_t>(gy0),
            static_cast<std::uint8_t>(gy1 >> 8), static_cast<std::uint8_t>(gy1)
        };
        send_command_data(CMD_RASET, d, sizeof(d));
    }

    // RAMWR — enter write mode; pixels follow.
    send_command(CMD_RAMWR);
}

bool write_pixels(const std::uint8_t* data, std::uint32_t byte_count) {
    spi::acquire();
    spi::cs_assert(kPinCs);
    spi::dc_data();
    const bool ok = spi::transmit(data, byte_count);
    spi::cs_deassert(kPinCs);
    spi::release();
    return ok;
}

}  // namespace st7789
