#include "persist.hpp"

namespace slate {
namespace persist {
namespace {
Hooks g_hooks{};
}  // namespace

void init(const Hooks& hooks) { g_hooks = hooks; }

std::size_t load(Slot slot, void* dst, std::size_t cap) {
  if (g_hooks.read == nullptr || dst == nullptr || cap == 0u) {
    return 0u;
  }
  return g_hooks.read(slot, dst, cap, g_hooks.ctx);
}

bool save(Slot slot, const void* src, std::size_t len) {
  if (g_hooks.write == nullptr || src == nullptr || len == 0u) {
    return false;
  }
  return g_hooks.write(slot, src, len, g_hooks.ctx);
}

}  // namespace persist
}  // namespace slate
