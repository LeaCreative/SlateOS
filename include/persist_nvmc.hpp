#pragma once

#include "persist.hpp"

#include <cstddef>
#include <cstdint>

// NVMC-backed persist page at end of internal flash (4 KB).

namespace slate {
namespace persist_nvmc {

#ifndef SLATE_PERSIST_FLASH_ADDR
#define SLATE_PERSIST_FLASH_ADDR 0x0007F000u
#endif

void init();

// Hooks suitable for persist::init.
std::size_t read_slot(persist::Slot slot, void* dst, std::size_t cap, void* ctx);
bool write_slot(persist::Slot slot, const void* src, std::size_t len, void* ctx);

}  // namespace persist_nvmc
}  // namespace slate
