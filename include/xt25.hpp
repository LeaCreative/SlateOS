#pragma once

#include <cstddef>
#include <cstdint>

// XT25F32B 4MB SPI NOR (CS=P0.05). Shares SPI0 with the display — all ops
// take spi::acquire()/release() around the CS pulse.

namespace slate {
namespace xt25 {

constexpr std::uint32_t kCapacityBytes = 4u * 1024u * 1024u;
constexpr std::uint32_t kPageSize = 256u;
constexpr std::uint32_t kSectorSize = 4096u;  // erase unit

constexpr std::uint8_t kCmdRead = 0x03u;
constexpr std::uint8_t kCmdPageProgram = 0x02u;
constexpr std::uint8_t kCmdSectorErase = 0x20u;
constexpr std::uint8_t kCmdWriteEnable = 0x06u;
constexpr std::uint8_t kCmdReadStatus = 0x05u;
constexpr std::uint8_t kCmdDeepPowerDown = 0xB9u;
constexpr std::uint8_t kCmdReleasePowerDown = 0xABu;
constexpr std::uint8_t kCmdJedecId = 0x9Fu;

void init();

// Wake from deep power-down if needed, then JEDEC ID check.
bool probe();

bool read(std::uint32_t addr, std::uint8_t* dst, std::size_t len);
bool write_page(std::uint32_t addr, const std::uint8_t* src, std::size_t len);
bool erase_sector(std::uint32_t addr);

// Idle path: CS high → CS low → 0xB9 → CS high.
void deep_power_down();
void wake();

bool is_busy();

}  // namespace xt25
}  // namespace slate
