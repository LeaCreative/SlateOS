#pragma once

#include "sdp_diag.hpp"
#include "slate_nimble_cfg.hpp"

#include <cstdint>

// Tracks NimBLE MSYS high-water usage for DIAG MBUF_REQ.
// Call note_free() whenever the pool is sampled (RX/TX paths).

namespace ble {

class MbufStatsTracker {
public:
  void reset() {
    min_free_ = SLATE_MSYS_1_BLOCK_COUNT;
    sampled_ = false;
  }

  void note_free(std::uint16_t free_now) {
    if (!sampled_ || free_now < min_free_) {
      min_free_ = free_now;
    }
    sampled_ = true;
    last_free_ = free_now;
  }

  sdp::diag::MbufReport report() const {
    sdp::diag::MbufReport r;
    r.block_count = SLATE_MSYS_1_BLOCK_COUNT;
    r.block_size = SLATE_MSYS_1_BLOCK_SIZE;
    r.free_now = last_free_;
    if (sampled_) {
      r.peak_used = static_cast<std::uint16_t>(
          SLATE_MSYS_1_BLOCK_COUNT > min_free_
              ? SLATE_MSYS_1_BLOCK_COUNT - min_free_
              : 0u);
    } else {
      r.peak_used = 0u;
      r.free_now = SLATE_MSYS_1_BLOCK_COUNT;
    }
    return r;
  }

private:
  std::uint16_t min_free_ = SLATE_MSYS_1_BLOCK_COUNT;
  std::uint16_t last_free_ = SLATE_MSYS_1_BLOCK_COUNT;
  bool sampled_ = false;
};

MbufStatsTracker& mbuf_stats();

}  // namespace ble
