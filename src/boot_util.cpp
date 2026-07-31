#include "boot_util.hpp"

#include "flash_map.hpp"
#include "nrf52832_regs.hpp"
#include "rtt.hpp"

#include <cstdint>

#if defined(SLATE_HOST_OTA) && (SLATE_HOST_OTA == 1)

namespace slate {
namespace boot {
bool needs_confirm() { return false; }
bool confirm_image() { return true; }
bool tick_confirm(bool, std::uint32_t) { return false; }
std::uint32_t dwell_remaining_ms(bool, std::uint32_t) { return 0u; }
}  // namespace boot
}  // namespace slate

#else

namespace slate {
namespace boot {
namespace {

// MCUBoot trailer (BOOT_MAX_ALIGN=8): from end of primary slot
//   magic[16]
//   image_ok[8]
//   copy_done[8]
//   ...
// image_ok is the first byte of the 8-byte field at (slot_end - 16 - 8).
constexpr std::uint32_t kBootMaxAlign = 8u;
constexpr std::uint32_t kMagicSz = 16u;
constexpr std::uint8_t kImageOkSet = 0x01u;
constexpr std::uint8_t kImageOkUnset = 0xFFu;

// Standard MCUBoot magic (little-endian words as bytes).
constexpr std::uint8_t kBootMagic[16] = {
    0x77u, 0xc2u, 0x95u, 0xf3u, 0x60u, 0xd2u, 0xefu, 0x7fu,
    0x35u, 0x52u, 0x50u, 0x0fu, 0x2cu, 0xb6u, 0x79u, 0x80u};

std::uint32_t g_connected_since_ms = 0u;
bool g_timing = false;

std::uint32_t image_ok_addr() {
  return flash_map::kPrimaryOffset + flash_map::kPrimarySize - kMagicSz -
         kBootMaxAlign;
}

std::uint32_t magic_addr() {
  return flash_map::kPrimaryOffset + flash_map::kPrimarySize - kMagicSz;
}

void nvmc_wait() {
  while (nrf::reg<std::uint32_t>(nrf::nvmc::READY) == 0u) {
  }
}

bool magic_present() {
  const auto* m = reinterpret_cast<const std::uint8_t*>(magic_addr());
  for (std::uint32_t i = 0u; i < kMagicSz; ++i) {
    if (m[i] != kBootMagic[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool needs_confirm() {
  if (!magic_present()) {
    // No MCUBoot trailer (bring-up / unsigned) — nothing to confirm.
    return false;
  }
  const std::uint8_t ok =
      *reinterpret_cast<const std::uint8_t*>(image_ok_addr());
  return ok == kImageOkUnset;
}

bool confirm_image() {
  if (!needs_confirm()) {
    return true;
  }
  // nRF52832 NVMC only accepts 32-bit writes to code flash. A STRB to the
  // IMAGE_OK byte escalates to HardFault (IMPRECISERR) — matches sealed
  // bisect1 dump CFSR=0x400 / PC in nearby rodata.
  //
  // Write the whole word as 1 rather than patching the low byte and leaving
  // 0xFFFFFF01. MCUBoot reads image_ok as a single byte, but InfiniTime's own
  // FirmwareValidator writes a word and compares the word against 1, so the
  // byte-patched form is only accepted by one of the two readers. NVMC can only
  // clear bits, and the trailer is erased after a swap, so 1 lands exactly.
  const std::uint32_t byte_addr = image_ok_addr();
  const std::uint32_t word_addr = byte_addr & ~3u;
  const std::uint32_t word = static_cast<std::uint32_t>(kImageOkSet);

  nvmc_wait();
  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = nrf::nvmc::CONFIG_WEN;
  nvmc_wait();
  *reinterpret_cast<volatile std::uint32_t*>(word_addr) = word;
  nvmc_wait();
  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = nrf::nvmc::CONFIG_REN;
  nvmc_wait();
  rtt::log(rtt::Level::Info, "boot: IMAGE_OK confirmed");
  return true;
}

bool tick_confirm(bool central_connected, std::uint32_t now_ms) {
  if (!central_connected) {
    g_timing = false;
    return false;
  }
  if (!g_timing) {
    g_connected_since_ms = now_ms;
    g_timing = true;
    return false;
  }
  // Unsigned dwell; requires now_ms wrap at 2^32 (slate::time::mono_ms).
  if (!dwell_satisfied(now_ms, g_connected_since_ms)) {
    return false;
  }
  if (!needs_confirm()) {
    return false;
  }
  return confirm_image();
}

std::uint32_t dwell_remaining_ms(bool central_connected, std::uint32_t now_ms) {
  if (!needs_confirm()) {
    return 0u;
  }
  if (!central_connected || !g_timing) {
    return kConfirmDwellMs;
  }
  const std::uint32_t elapsed =
      static_cast<std::uint32_t>(now_ms - g_connected_since_ms);
  if (elapsed >= kConfirmDwellMs) {
    return 0u;
  }
  return kConfirmDwellMs - elapsed;
}

}  // namespace boot
}  // namespace slate

#endif
