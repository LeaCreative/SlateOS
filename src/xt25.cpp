#include "xt25.hpp"

#include "board.hpp"
#include "spi_bus.hpp"

namespace slate {
namespace xt25 {
namespace {

bool g_asleep = false;

void cmd_only(std::uint8_t cmd) {
  spi::acquire();
  spi::cs_assert(spi::kPinCsFlash);
  spi::transmit(&cmd, 1u);
  spi::cs_deassert(spi::kPinCsFlash);
  spi::release();
}

std::uint8_t read_status() {
  const std::uint8_t cmd = kCmdReadStatus;
  std::uint8_t st = 0xFFu;
  spi::acquire();
  spi::cs_assert(spi::kPinCsFlash);
  spi::transfer(&cmd, 1u, &st, 1u);
  spi::cs_deassert(spi::kPinCsFlash);
  spi::release();
  return st;
}

void wait_ready() {
  for (int i = 0; i < 100000; ++i) {
    if ((read_status() & 0x01u) == 0u) {
      return;
    }
    board::busy_wait_us(50u);
  }
}

void ensure_awake() {
  if (!g_asleep) {
    return;
  }
  cmd_only(kCmdReleasePowerDown);
  board::busy_wait_us(50u);  // tRES1
  g_asleep = false;
}

}  // namespace

void init() {
  g_asleep = false;
  // CS already high from spi::init.
}

bool probe() {
  ensure_awake();
  const std::uint8_t cmd = kCmdJedecId;
  std::uint8_t id[3] = {};
  spi::acquire();
  spi::cs_assert(spi::kPinCsFlash);
  spi::transfer(&cmd, 1u, id, 3u);
  spi::cs_deassert(spi::kPinCsFlash);
  spi::release();
  // XT25F32B: manufacturer 0x0B (XMC) or compatible; accept non-zero mid.
  return id[0] != 0x00u && id[0] != 0xFFu;
}

bool read(std::uint32_t addr, std::uint8_t* dst, std::size_t len) {
  if (dst == nullptr || len == 0u || addr + len > kCapacityBytes) {
    return false;
  }
  ensure_awake();
  std::uint8_t hdr[4] = {
      kCmdRead,
      static_cast<std::uint8_t>((addr >> 16) & 0xFFu),
      static_cast<std::uint8_t>((addr >> 8) & 0xFFu),
      static_cast<std::uint8_t>(addr & 0xFFu),
  };
  spi::acquire();
  spi::cs_assert(spi::kPinCsFlash);
  spi::transfer(hdr, sizeof(hdr), dst, len);
  spi::cs_deassert(spi::kPinCsFlash);
  spi::release();
  return true;
}

bool write_page(std::uint32_t addr, const std::uint8_t* src, std::size_t len) {
  if (src == nullptr || len == 0u || len > kPageSize ||
      (addr % kPageSize) + len > kPageSize || addr + len > kCapacityBytes) {
    return false;
  }
  ensure_awake();
  cmd_only(kCmdWriteEnable);
  std::uint8_t hdr[4] = {
      kCmdPageProgram,
      static_cast<std::uint8_t>((addr >> 16) & 0xFFu),
      static_cast<std::uint8_t>((addr >> 8) & 0xFFu),
      static_cast<std::uint8_t>(addr & 0xFFu),
  };
  // Concatenate header+data into a stack buffer (page ≤ 256).
  std::uint8_t buf[4 + 256];
  buf[0] = hdr[0];
  buf[1] = hdr[1];
  buf[2] = hdr[2];
  buf[3] = hdr[3];
  for (std::size_t i = 0u; i < len; ++i) {
    buf[4u + i] = src[i];
  }
  spi::acquire();
  spi::cs_assert(spi::kPinCsFlash);
  spi::transmit(buf, 4u + len);
  spi::cs_deassert(spi::kPinCsFlash);
  spi::release();
  wait_ready();
  return true;
}

bool erase_sector(std::uint32_t addr) {
  if (addr % kSectorSize != 0u || addr >= kCapacityBytes) {
    return false;
  }
  ensure_awake();
  cmd_only(kCmdWriteEnable);
  std::uint8_t hdr[4] = {
      kCmdSectorErase,
      static_cast<std::uint8_t>((addr >> 16) & 0xFFu),
      static_cast<std::uint8_t>((addr >> 8) & 0xFFu),
      static_cast<std::uint8_t>(addr & 0xFFu),
  };
  spi::acquire();
  spi::cs_assert(spi::kPinCsFlash);
  spi::transmit(hdr, sizeof(hdr));
  spi::cs_deassert(spi::kPinCsFlash);
  spi::release();
  wait_ready();
  return true;
}

void deep_power_down() {
  if (g_asleep) {
    return;
  }
  // Spec: CS high, CS low, write 0xB9, CS high.
  spi::cs_deassert(spi::kPinCsFlash);
  cmd_only(kCmdDeepPowerDown);
  g_asleep = true;
}

void wake() { ensure_awake(); }

bool is_busy() {
  ensure_awake();
  return (read_status() & 0x01u) != 0u;
}

}  // namespace xt25
}  // namespace slate
