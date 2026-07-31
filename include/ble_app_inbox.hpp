#pragma once

#include "sdp_frame.hpp"

#include <cstddef>
#include <cstdint>

// Link → app handoff (N-1 / I-10 stage 1): NimBLE host task must not run
// session/parse/render. The app task drains and dispatches.
//
// RAM choice (N-8): ZERO-COPY. The inbox borrows the reassembler's buffer —
// it stores only {pointer, channel, len}, not a 4 KiB copy (the original
// copy-slot design put heap past the MSP stack reserve and the image no
// longer linked). Safety contract: while a message is pending, the producer
// (Link) must not ingest anything — the reassembler would overwrite the
// borrowed bytes. Fragments arriving while busy are dropped and counted;
// SDP CREDIT is only emitted after the app task finishes apply, so a
// well-behaved companion waits and never overruns. CONTROL / SYSTEM / OTA
// share the same slot (per-channel order preserved because there is a single
// in-flight message globally).

namespace ble {

class AppInbox {
public:
  static constexpr std::size_t kSlotBytes = sdp::frame::kMaxMessageBytes;

  void reset() {
    occupied_ = 0u;
    msg_ = nullptr;
    channel_ = 0u;
    len_ = 0u;
    drops_ = 0u;
  }

  /**
   * Host-task / producer. Borrows [msg,len] until drain_one; false if busy or
   * oversized. The caller must keep the bytes untouched while busy().
   */
  bool try_push(std::uint8_t channel, const std::uint8_t* msg, std::size_t len) {
    if (msg == nullptr || len == 0u || len > kSlotBytes) {
      return false;
    }
    if (occupied_ != 0u) {
      ++drops_;
      return false;
    }
    msg_ = msg;
    channel_ = channel;
    len_ = static_cast<std::uint16_t>(len);
    // Release: publish payload before occupied becomes visible to the app task.
    occupied_ = 1u;
    return true;
  }

  /** Producer bookkeeping: a fragment was discarded because the slot is busy. */
  void note_busy_drop() { ++drops_; }

  /**
   * App-task / consumer. Invokes fn once if a message is pending, then frees
   * the slot. Returns true if a message was delivered.
   */
  template <typename Fn>
  bool drain_one(Fn&& fn) {
    if (occupied_ == 0u) {
      return false;
    }
    const std::uint8_t ch = channel_;
    const std::uint16_t n = len_;
    fn(ch, msg_, static_cast<std::size_t>(n));
    occupied_ = 0u;
    return true;
  }

  bool busy() const { return occupied_ != 0u; }
  std::uint32_t drop_count() const { return drops_; }

  /** Static footprint for RAM accounting / host tests. */
  static constexpr std::size_t storage_bytes() {
    return sizeof(const std::uint8_t*) + sizeof(std::uint16_t) +
           sizeof(std::uint8_t) * 2u + sizeof(std::uint32_t);
  }

private:
  const std::uint8_t* msg_ = nullptr;
  std::uint16_t len_ = 0u;
  std::uint8_t channel_ = 0u;
  volatile std::uint8_t occupied_ = 0u;
  std::uint32_t drops_ = 0u;
};

}  // namespace ble
