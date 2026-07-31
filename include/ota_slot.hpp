#pragma once

#include <cstddef>
#include <cstdint>

// External SECONDARY slot writer (XT25). Host builds stub via SLATE_HOST_OTA=1.

namespace slate {
namespace ota_slot {

void init();

// Erase the entire secondary slot (sector by sector). Slow; call on BEGIN.
bool erase_all();

// Program bytes at offset within secondary (0 .. kSecondarySize).
bool write(std::uint32_t offset, const std::uint8_t* data, std::size_t len);

// Read back for hash verify (optional; OTA may hash on the fly).
bool read(std::uint32_t offset, std::uint8_t* dst, std::size_t len);

std::uint32_t capacity();

// After a valid image is fully written, request MCU reset so MCUBoot swaps.
void request_reboot();

// InfiniTime MCUBoot pending-image magic at end of secondary slot.
// Call after a complete DFU / channel-5 image write, before reboot.
bool write_infinitime_pending_magic();

}  // namespace ota_slot
}  // namespace slate
