#include "lfs_fs.hpp"

#include "xt25.hpp"

#include "lfs.h"

#include <cstring>

namespace slate {
namespace fs {
namespace {

lfs_t g_lfs{};
lfs_config g_cfg{};
bool g_mounted = false;

// LittleFS look-ahead / cache buffers — kept small (in RAM budget).
alignas(4) std::uint8_t g_read_buf[256];
alignas(4) std::uint8_t g_prog_buf[256];
alignas(4) std::uint8_t g_lookahead[64];

int bd_read(const struct lfs_config* c, lfs_block_t block, lfs_off_t off,
            void* buffer, lfs_size_t size) {
  (void)c;
  const std::uint32_t addr =
      static_cast<std::uint32_t>(block) * xt25::kSectorSize + off;
  return xt25::read(addr, static_cast<std::uint8_t*>(buffer), size) ? 0
                                                                    : LFS_ERR_IO;
}

int bd_prog(const struct lfs_config* c, lfs_block_t block, lfs_off_t off,
            const void* buffer, lfs_size_t size) {
  (void)c;
  const std::uint32_t addr =
      static_cast<std::uint32_t>(block) * xt25::kSectorSize + off;
  const auto* p = static_cast<const std::uint8_t*>(buffer);
  std::size_t left = size;
  std::uint32_t a = addr;
  while (left > 0u) {
    const std::size_t n = (left > xt25::kPageSize) ? xt25::kPageSize : left;
    // Page program must not cross page boundary — caller/LFS respects prog_size.
    if (!xt25::write_page(a, p, n)) {
      return LFS_ERR_IO;
    }
    p += n;
    a += static_cast<std::uint32_t>(n);
    left -= n;
  }
  return 0;
}

int bd_erase(const struct lfs_config* c, lfs_block_t block) {
  (void)c;
  const std::uint32_t addr = static_cast<std::uint32_t>(block) * xt25::kSectorSize;
  return xt25::erase_sector(addr) ? 0 : LFS_ERR_IO;
}

int bd_sync(const struct lfs_config* c) {
  (void)c;
  return 0;
}

void fill_config() {
  std::memset(&g_cfg, 0, sizeof(g_cfg));
  g_cfg.read = bd_read;
  g_cfg.prog = bd_prog;
  g_cfg.erase = bd_erase;
  g_cfg.sync = bd_sync;
  g_cfg.read_size = 16;
  g_cfg.prog_size = 16;
  g_cfg.block_size = xt25::kSectorSize;
  g_cfg.block_count = xt25::kCapacityBytes / xt25::kSectorSize;
  g_cfg.cache_size = 256;
  g_cfg.lookahead_size = 64;
  g_cfg.block_cycles = 500;
  g_cfg.read_buffer = g_read_buf;
  g_cfg.prog_buffer = g_prog_buf;
  g_cfg.lookahead_buffer = g_lookahead;
}

}  // namespace

bool mount() {
  if (g_mounted) {
    return true;
  }
  xt25::init();
  xt25::wake();
  (void)xt25::probe();
  fill_config();
  int err = lfs_mount(&g_lfs, &g_cfg);
  if (err != 0) {
    err = lfs_format(&g_lfs, &g_cfg);
    if (err != 0) {
      return false;
    }
    err = lfs_mount(&g_lfs, &g_cfg);
    if (err != 0) {
      return false;
    }
  }
  // Ensure /assets directory.
  lfs_mkdir(&g_lfs, "/assets");
  g_mounted = true;
  return true;
}

void unmount() {
  if (!g_mounted) {
    return;
  }
  lfs_unmount(&g_lfs);
  g_mounted = false;
  xt25::deep_power_down();
}

bool mounted() { return g_mounted; }

void sleep_flash() {
  if (g_mounted) {
    // Best-effort sync via unmount is too heavy; just DPD the chip.
  }
  xt25::deep_power_down();
}

void wake_flash() { xt25::wake(); }

std::size_t read_file(const char* path, std::uint32_t offset, std::uint8_t* dst,
                      std::size_t len) {
  if (!g_mounted || path == nullptr || dst == nullptr || len == 0u) {
    return 0u;
  }
  xt25::wake();
  lfs_file_t file;
  if (lfs_file_open(&g_lfs, &file, path, LFS_O_RDONLY) != 0) {
    return 0u;
  }
  if (lfs_file_seek(&g_lfs, &file, static_cast<lfs_soff_t>(offset), LFS_SEEK_SET) <
      0) {
    lfs_file_close(&g_lfs, &file);
    return 0u;
  }
  const lfs_ssize_t n = lfs_file_read(&g_lfs, &file, dst, len);
  lfs_file_close(&g_lfs, &file);
  return n > 0 ? static_cast<std::size_t>(n) : 0u;
}

std::size_t write_file_replace(const char* path, const std::uint8_t* src,
                               std::size_t len) {
  if (!g_mounted || path == nullptr || src == nullptr) {
    return 0u;
  }
  xt25::wake();
  lfs_file_t file;
  if (lfs_file_open(&g_lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) !=
      0) {
    return 0u;
  }
  const lfs_ssize_t n = lfs_file_write(&g_lfs, &file, src, len);
  lfs_file_close(&g_lfs, &file);
  return n > 0 ? static_cast<std::size_t>(n) : 0u;
}

bool write_file_at(const char* path, std::uint32_t offset, const std::uint8_t* src,
                   std::size_t len) {
  if (!g_mounted || path == nullptr || src == nullptr || len == 0u) {
    return false;
  }
  xt25::wake();
  lfs_file_t file;
  if (lfs_file_open(&g_lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT) != 0) {
    return false;
  }
  if (lfs_file_seek(&g_lfs, &file, static_cast<lfs_soff_t>(offset), LFS_SEEK_SET) <
      0) {
    lfs_file_close(&g_lfs, &file);
    return false;
  }
  const lfs_ssize_t n = lfs_file_write(&g_lfs, &file, src, len);
  lfs_file_close(&g_lfs, &file);
  return n == static_cast<lfs_ssize_t>(len);
}

bool remove_file(const char* path) {
  if (!g_mounted || path == nullptr) {
    return false;
  }
  xt25::wake();
  return lfs_remove(&g_lfs, path) == 0;
}

bool exists(const char* path) {
  if (!g_mounted || path == nullptr) {
    return false;
  }
  lfs_info info{};
  return lfs_stat(&g_lfs, path, &info) == 0;
}

}  // namespace fs
}  // namespace slate
