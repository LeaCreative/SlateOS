#include "wall_clock.hpp"

#include "persist.hpp"

#include <cstddef>

namespace slate {
namespace clock {
namespace {

Hooks g_hooks{};
PersistBlob g_blob{};
bool g_synced = false;

constexpr std::uint64_t kTicksPerSec = 32768u;

std::uint32_t crc32(const std::uint8_t* p, std::size_t n) {
  std::uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0u; i < n; ++i) {
    c ^= p[i];
    for (int b = 0; b < 8; ++b) {
      const std::uint32_t m = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(c & 1u)));
      c = (c >> 1) ^ (0xEDB88320u & m);
    }
  }
  return ~c;
}

std::uint64_t now_ticks() {
  if (g_hooks.ticks == nullptr) {
    return 0u;
  }
  return g_hooks.ticks(g_hooks.ctx);
}

bool is_leap(std::uint16_t y) {
  return (y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u);
}

}  // namespace

void init(const Hooks& hooks) {
  g_hooks = hooks;
  g_blob = PersistBlob{};
  g_synced = false;
}

void load_persisted() {
  PersistBlob tmp{};
  const std::size_t n = persist::load(persist::Slot::Clock, &tmp, sizeof(tmp));
  if (n < sizeof(tmp) || tmp.magic != kPersistMagic) {
    return;
  }
  const std::uint32_t expect = tmp.crc;
  tmp.crc = 0u;
  if (crc32(reinterpret_cast<const std::uint8_t*>(&tmp), sizeof(tmp)) != expect) {
    return;
  }
  g_blob = tmp;
  g_synced = (g_blob.epoch_at_sync != 0u);
}

void save_persisted() {
  g_blob.magic = kPersistMagic;
  g_blob.crc = 0u;
  g_blob.crc = crc32(reinterpret_cast<const std::uint8_t*>(&g_blob), sizeof(g_blob));
  (void)persist::save(persist::Slot::Clock, &g_blob, sizeof(g_blob));
}

void apply_cts_sync(std::uint32_t unix_epoch_sec) {
  const std::uint64_t t = now_ticks();
  // First sync may land at tick 0 — still a valid anchor; only skip drift
  // until we have a prior sync (g_synced) and a positive tick delta.
  if (g_synced && t > g_blob.ticks_at_sync) {
    const std::uint64_t dt_ticks = t - g_blob.ticks_at_sync;
    const std::uint32_t local_elapsed =
        static_cast<std::uint32_t>(dt_ticks / kTicksPerSec);
    // Apply previous drift so we measure residual error on the corrected clock.
    const std::int64_t corrected =
        static_cast<std::int64_t>(local_elapsed) +
        (static_cast<std::int64_t>(local_elapsed) * g_blob.drift_ppm) / 1000000;
    const std::int64_t phone_elapsed =
        static_cast<std::int64_t>(unix_epoch_sec) -
        static_cast<std::int64_t>(g_blob.epoch_at_sync);
    if (corrected > 30 && phone_elapsed > 30) {
      // ppm = (phone - local) / local * 1e6, clamped.
      const std::int64_t err = phone_elapsed - corrected;
      std::int64_t ppm = (err * 1000000) / corrected;
      if (ppm > 5000) ppm = 5000;
      if (ppm < -5000) ppm = -5000;
      // Blend toward new estimate (EMA) so one bad sync cannot swing hard.
      g_blob.drift_ppm = static_cast<std::int32_t>(
          (static_cast<std::int64_t>(g_blob.drift_ppm) * 3 + ppm) / 4);
    }
  }
  g_blob.epoch_at_sync = unix_epoch_sec;
  g_blob.ticks_at_sync = t;
  g_synced = true;
  save_persisted();
}

std::uint32_t unix_time() {
  if (!g_synced) {
    return 0u;
  }
  const std::uint64_t t = now_ticks();
  if (t < g_blob.ticks_at_sync) {
    return g_blob.epoch_at_sync;
  }
  const std::uint64_t dt = t - g_blob.ticks_at_sync;
  std::uint64_t secs = dt / kTicksPerSec;
  // Apply ppm: add (secs * ppm) / 1e6
  const std::int64_t adj =
      (static_cast<std::int64_t>(secs) * g_blob.drift_ppm) / 1000000;
  std::int64_t out =
      static_cast<std::int64_t>(g_blob.epoch_at_sync) +
      static_cast<std::int64_t>(secs) + adj;
  if (out < 0) {
    out = 0;
  }
  return static_cast<std::uint32_t>(out);
}

Civil civil_now() {
  Civil c{};
  std::uint32_t t = unix_time();
  c.wday = static_cast<std::uint8_t>((t / 86400u + 4u) % 7u);  // 1970-01-01 Thu=4
  c.second = static_cast<std::uint8_t>(t % 60u);
  t /= 60u;
  c.minute = static_cast<std::uint8_t>(t % 60u);
  t /= 60u;
  c.hour = static_cast<std::uint8_t>(t % 24u);
  t /= 24u;  // days since epoch

  std::uint16_t year = 1970u;
  while (true) {
    const std::uint32_t diy = is_leap(year) ? 366u : 365u;
    if (t < diy) {
      break;
    }
    t -= diy;
    ++year;
  }
  c.year = year;
  static const std::uint8_t kDim[12] = {31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
  for (std::uint8_t m = 1u; m <= 12u; ++m) {
    std::uint8_t dim = kDim[m - 1u];
    if (m == 2u && is_leap(year)) {
      dim = 29u;
    }
    if (t < dim) {
      c.month = m;
      c.day = static_cast<std::uint8_t>(t + 1u);
      return c;
    }
    t -= dim;
  }
  c.month = 12u;
  c.day = 31u;
  return c;
}

std::int32_t drift_ppm() { return g_blob.drift_ppm; }

bool is_synced() { return g_synced; }

void set_for_test(std::uint32_t epoch, std::int32_t ppm) {
  g_blob.epoch_at_sync = epoch;
  g_blob.ticks_at_sync = now_ticks();
  g_blob.drift_ppm = ppm;
  g_synced = true;
}

}  // namespace clock
}  // namespace slate
