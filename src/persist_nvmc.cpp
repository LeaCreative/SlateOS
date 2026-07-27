#include "persist_nvmc.hpp"

#include "nrf52832_regs.hpp"

#include <cstring>

namespace slate {
namespace persist_nvmc {
namespace {

constexpr std::uint32_t kPageSize = 4096u;
constexpr std::uint32_t kSlotBytes = 1024u;
constexpr std::uintptr_t kErase = nrf::NVMC_BASE + 0x508u;
constexpr std::uint32_t kConfigEen = 2u;

struct SlotHdr {
  std::uint32_t magic;
  std::uint32_t len;
};

constexpr std::uint32_t kMagic = 0x53504C54u;  // 'SPLT'

std::uintptr_t slot_addr(persist::Slot slot) {
  return SLATE_PERSIST_FLASH_ADDR +
         static_cast<std::uint32_t>(slot) * kSlotBytes;
}

void nvmc_wait() {
  while (nrf::reg<std::uint32_t>(nrf::nvmc::READY) == 0u) {
  }
}

void erase_page() {
  nvmc_wait();
  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = kConfigEen;
  nvmc_wait();
  nrf::reg<std::uint32_t>(kErase) = SLATE_PERSIST_FLASH_ADDR;
  nvmc_wait();
  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = nrf::nvmc::CONFIG_REN;
  nvmc_wait();
}

// Staging: rewrite whole page (3 slots) on any write — fine for rare settings.
alignas(4) std::uint8_t g_page_cache[kPageSize];
bool g_cache_valid = false;

void load_cache() {
  if (g_cache_valid) {
    return;
  }
  std::memcpy(g_page_cache, reinterpret_cast<const void*>(SLATE_PERSIST_FLASH_ADDR),
              kPageSize);
  g_cache_valid = true;
}

void flush_cache() {
  erase_page();
  nvmc_wait();
  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = nrf::nvmc::CONFIG_WEN;
  nvmc_wait();
  auto* dst = reinterpret_cast<volatile std::uint32_t*>(SLATE_PERSIST_FLASH_ADDR);
  const auto* src = reinterpret_cast<const std::uint32_t*>(g_page_cache);
  for (std::size_t i = 0u; i < kPageSize / 4u; ++i) {
    dst[i] = src[i];
    nvmc_wait();
  }
  nrf::reg<std::uint32_t>(nrf::nvmc::CONFIG) = nrf::nvmc::CONFIG_REN;
  nvmc_wait();
}

}  // namespace

void init() {
  g_cache_valid = false;
  load_cache();
}

std::size_t read_slot(persist::Slot slot, void* dst, std::size_t cap, void*) {
  if (dst == nullptr || cap == 0u ||
      static_cast<std::uint8_t>(slot) >= static_cast<std::uint8_t>(persist::Slot::Count)) {
    return 0u;
  }
  load_cache();
  const std::size_t off = static_cast<std::size_t>(slot) * kSlotBytes;
  const auto* hdr = reinterpret_cast<const SlotHdr*>(g_page_cache + off);
  if (hdr->magic != kMagic || hdr->len == 0u || hdr->len > kSlotBytes - sizeof(SlotHdr)) {
    return 0u;
  }
  const std::size_t n = hdr->len < cap ? hdr->len : cap;
  std::memcpy(dst, g_page_cache + off + sizeof(SlotHdr), n);
  return n;
}

bool write_slot(persist::Slot slot, const void* src, std::size_t len, void*) {
  if (src == nullptr || len == 0u || len > kSlotBytes - sizeof(SlotHdr) ||
      static_cast<std::uint8_t>(slot) >= static_cast<std::uint8_t>(persist::Slot::Count)) {
    return false;
  }
  load_cache();
  const std::size_t off = static_cast<std::size_t>(slot) * kSlotBytes;
  auto* hdr = reinterpret_cast<SlotHdr*>(g_page_cache + off);
  hdr->magic = kMagic;
  hdr->len = static_cast<std::uint32_t>(len);
  std::memcpy(g_page_cache + off + sizeof(SlotHdr), src, len);
  // Clear rest of slot.
  std::memset(g_page_cache + off + sizeof(SlotHdr) + len, 0xFF,
              kSlotBytes - sizeof(SlotHdr) - len);
  flush_cache();
  return true;
}

}  // namespace persist_nvmc
}  // namespace slate
