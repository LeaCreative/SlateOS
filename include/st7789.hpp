#pragma once

#include <cstdint>

// ST7789 display driver.
//
// Assumes spi::init() has already been called.  The driver owns the display CS
// and RST pins; the SPI bus is shared with the flash driver via spi::acquire().
//
// Coordinate system: (0,0) = top-left, x increases right, y increases down.
// Resolution: 240 × 240, RGB565 big-endian on the wire (ST7789 default).

namespace st7789 {

static constexpr std::uint32_t kWidth  = 240u;
static constexpr std::uint32_t kHeight = 240u;

// Initialise the display: hard reset, command sequence, display on.
// Leaves the display in sleep-out, normal display mode.
void init();

// Power management — call before/after long idle periods.
void sleep_in();
void sleep_out();

// Set the write window for the next pixel burst.
// x0/y0 inclusive, x1/y1 inclusive.
void set_window(std::uint16_t x0, std::uint16_t y0,
                std::uint16_t x1, std::uint16_t y1);

// Write `pixel_count` RGB565 pixels (big-endian) from `data` to the display.
// Window must have been set with set_window() first.
// Acquires and releases the SPI bus internally.
void write_pixels(const std::uint8_t* data, std::uint32_t byte_count);

}  // namespace st7789
