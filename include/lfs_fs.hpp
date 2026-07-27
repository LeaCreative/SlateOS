#pragma once

#include <cstddef>
#include <cstdint>

// LittleFS mount over XT25F32B. Host builds may stub with SLATE_HOST_LFS=1.

namespace slate {
namespace fs {

bool mount();
void unmount();
bool mounted();

// Enter deep power-down on the NOR when idle (after sync).
void sleep_flash();
void wake_flash();

// Read/write file helpers for asset packs. Paths are LittleFS absolute
// (e.g. "/assets/core.slap"). Returns bytes read/written or 0 on error.
std::size_t read_file(const char* path, std::uint32_t offset, std::uint8_t* dst,
                      std::size_t len);
std::size_t write_file_replace(const char* path, const std::uint8_t* src,
                               std::size_t len);
// Create/extend file and write at absolute offset (ASSET staging).
bool write_file_at(const char* path, std::uint32_t offset, const std::uint8_t* src,
                   std::size_t len);
bool remove_file(const char* path);
bool exists(const char* path);

}  // namespace fs
}  // namespace slate
