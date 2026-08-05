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

namespace {
// Bytes from the start of the slot known to be erased. Sectors are erased
// lazily as writes advance (N-20) — see erase_all().
std::uint32_t g_erased_upto = 0u;

// Page-program without any erase bookkeeping. Callers must have ensured the
// target sector is erased.
bool write_raw(std::uint32_t offset, const std::uint8_t* data, std::size_t len) {
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

/** Erase forward until `end` is covered. Sequential writes touch each
 *  sector exactly once, so this costs one sector erase per 4 KiB. */
bool ensure_erased(std::uint32_t end) {
  if (end > flash_map::kSecondarySize) {
    return false;
  }
  while (g_erased_upto < end) {
    slate::wdt::pet();
    if (!xt25::erase_sector(flash_map::kSecondaryOffset + g_erased_upto)) {
      return false;
    }
    g_erased_upto += xt25::kSectorSize;
  }
  return true;
}
}  // namespace

void init() {
  xt25::wake();
  g_erased_upto = 0u;
}

/**
 * Arm the slot for a new image — deliberately does NOT erase 116 sectors here.
 *
 * It used to (N-20). At the XT25's 300 ms worst-case sector erase that is up
 * to ~35 s of a fully blocked caller, and both update paths call it from a
 * context that cannot afford to disappear: Nordic DFU runs in the GATT
 * callback on the NimBLE **host task**, so the link itself died — nRF Connect
 * logged 29.4 s of silence after the size packet, then a disconnect. SDP OTA
 * calls it from the app task at BEGIN, which starved the drain and dropped
 * every chunk that arrived meanwhile.
 *
 * Erasing lazily spreads the same work across the transfer: one ~60–300 ms
 * sector erase per 4 KiB written, which fits comfortably inside a single
 * chunk write. Both writers are strictly sequential from offset 0, so each
 * sector is still erased exactly once, before anything is written to it.
 */
bool erase_all() {
  xt25::wake();
  g_erased_upto = 0u;
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
  // Erase forward to cover this write before touching the flash (N-20).
  if (!ensure_erased(offset + static_cast<std::uint32_t>(len))) {
    return false;
  }
  return write_raw(offset, data, len);
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
  // The trailer sits in the last sector, far beyond the image end. Erase that
  // one sector directly instead of letting ensure_erased() walk the whole gap
  // — on a 133 KiB image into a 464 KiB slot that gap is ~83 sectors, which
  // would reintroduce exactly the multi-second stall N-20 removed.
  xt25::wake();
  const std::uint32_t sector_base =
      (off / xt25::kSectorSize) * xt25::kSectorSize;
  if (sector_base >= g_erased_upto) {
    slate::wdt::pet();
    if (!xt25::erase_sector(flash_map::kSecondaryOffset + sector_base)) {
      return false;
    }
  }
  return write_raw(off, reinterpret_cast<const std::uint8_t*>(magic),
                   sizeof(magic));
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
