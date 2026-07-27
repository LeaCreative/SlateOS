#pragma once

#include <cstddef>
#include <cstdint>

// Tiny flash-backed key/value blobs (pre-LittleFS). Host injects a RAM backend.

namespace slate {
namespace persist {

enum class Slot : std::uint8_t {
  Clock = 0,
  Alarms = 1,
  Settings = 2,
  Count = 3,
};

struct Hooks {
  // Read `cap` bytes from `slot` into `dst`. Return bytes read (0 = empty/missing).
  std::size_t (*read)(Slot slot, void* dst, std::size_t cap, void* ctx) = nullptr;
  // Persist `len` bytes. Return true on success.
  bool (*write)(Slot slot, const void* src, std::size_t len, void* ctx) = nullptr;
  void* ctx = nullptr;
};

void init(const Hooks& hooks);

std::size_t load(Slot slot, void* dst, std::size_t cap);
bool save(Slot slot, const void* src, std::size_t len);

}  // namespace persist
}  // namespace slate
