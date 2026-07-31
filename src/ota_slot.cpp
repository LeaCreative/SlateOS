#include "ota_slot.hpp"

#include "flash_map.hpp"
#include "wdt.hpp"
#include "xt25.hpp"

#include <cstdint>

#if defined(SLATE_HOST_OTA) && (SLATE_HOST_OTA == 1)
// Host stub — see tests.
namespace slate {
namespace ota_slot {
void init() {}
bool erase_all() { return true; }
bool write(std::uint32_t, const std::uint8_t*, std::size_t) { return true; }
bool read(std::uint32_t, std::uint8_t*, std::size_t) { return false; }
std::uint32_t capacity() { return flash_map::kSecondarySize; }
bool write_infinitime_pending_magic() { return true; }
void request_reboot() {}
}  // namespace ota_slot
}  // namespace slate
#else

namespace slate {
namespace ota_slot {

void init() {
  xt25::wake();
}

bool erase_all() {
  xt25::wake();
  for (std::uint32_t off = 0u; off < flash_map::kSecondarySize;
       off += xt25::kSectorSize) {
    // Secondary is ~464 KiB / 4 KiB ≈ 116 sector erases. Each can take hundreds
    // of ms; without a pet between them a single BEGIN can exceed the ~7 s
    // bootloader WDT even though xt25::wait_ready also pets.
    slate::wdt::pet();
    if (!xt25::erase_sector(flash_map::kSecondaryOffset + off)) {
      return false;
    }
  }
  return true;
}

bool write(std::uint32_t offset, const std::uint8_t* data, std::size_t len) {
  if (data == nullptr || len == 0u) {
    return false;
  }
  if (offset > flash_map::kSecondarySize ||
      len > flash_map::kSecondarySize - offset) {
    return false;
  }
  xt25::wake();
  std::uint32_t addr = flash_map::kSecondaryOffset + offset;
  const std::uint8_t* p = data;
  std::size_t left = len;
  while (left > 0u) {
    const std::uint32_t page_off = addr % xt25::kPageSize;
    const std::size_t space = xt25::kPageSize - page_off;
    const std::size_t n = (left < space) ? left : space;
    if (!xt25::write_page(addr, p, n)) {
      return false;
    }
    addr += static_cast<std::uint32_t>(n);
    p += n;
    left -= n;
  }
  return true;
}

bool read(std::uint32_t offset, std::uint8_t* dst, std::size_t len) {
  if (dst == nullptr || len == 0u) {
    return false;
  }
  if (offset > flash_map::kSecondarySize ||
      len > flash_map::kSecondarySize - offset) {
    return false;
  }
  xt25::wake();
  return xt25::read(flash_map::kSecondaryOffset + offset, dst, len);
}

std::uint32_t capacity() { return flash_map::kSecondarySize; }

bool write_infinitime_pending_magic() {
  // Same layout as InfiniTime DfuImage::WriteMagicNumber — last 16 bytes of slot.
  alignas(4) std::uint32_t magic[4] = {
      flash_map::kDfuMagic0,
      flash_map::kDfuMagic1,
      flash_map::kDfuMagic2,
      flash_map::kDfuMagic3,
  };
  const std::uint32_t off =
      flash_map::kSecondarySize - static_cast<std::uint32_t>(sizeof(magic));
  return write(off, reinterpret_cast<const std::uint8_t*>(magic), sizeof(magic));
}

void request_reboot() {
  // AIRCR SYSRESETREQ
  constexpr std::uintptr_t kAircr = 0xE000ED0Cu;
  *reinterpret_cast<volatile std::uint32_t*>(kAircr) =
      (0x05FAu << 16) | (1u << 2);
  while (true) {
  }
}

}  // namespace ota_slot
}  // namespace slate

#endif
