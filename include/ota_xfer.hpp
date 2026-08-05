#pragma once

#include "sha256.hpp"

#include <cstddef>
#include <cstdint>

// SDP channel 5 OTA — chunked, resumable, SHA-256 verified, 30% battery gate.

namespace slate {
namespace ota {

constexpr std::uint8_t kOpBegin = 0x01u;
constexpr std::uint8_t kOpChunk = 0x02u;
constexpr std::uint8_t kOpAbort = 0x03u;
constexpr std::uint8_t kOpCommit = 0x04u;

constexpr std::uint8_t kOpCredit = 0x10u;
constexpr std::uint8_t kOpAck = 0x11u;
constexpr std::uint8_t kOpNak = 0x12u;

constexpr std::size_t kMaxBytesPerQuantum = 512u;

// One chunk in flight, not four (N-19). The link hands the app task exactly
// one message at a time — `ble::AppInbox` is a single zero-copy slot, and
// anything arriving while it is occupied is dropped — so a 2048-byte window
// meant the sender always put four chunks on the air when the watch could
// absorb one. Three were discarded every burst, and each burst then needed a
// 15 s timeout plus a resume handshake to recover; a real transfer crawled to
// 10752/133252 B before exhausting its resync budget. Matching the window to
// the receiver's true capacity makes the exchange lock-step: send one chunk,
// get ACK + fresh credit, send the next. Slower on paper than a sliding
// window, far faster than one that spends most of its time recovering.
constexpr std::uint16_t kWindowBytes =
    static_cast<std::uint16_t>(kMaxBytesPerQuantum);

enum class NakReason : std::uint8_t {
  Ok = 0,
  Busy = 1,
  BadMessage = 2,
  HashFail = 3,
  TooLarge = 4,
  NoStorage = 5,
  LowBattery = 6,
  Yield = 7,
  // The running image is still on trial (I-13). Stacking a new image on an
  // unconfirmed one means a revert lands on firmware nobody has validated,
  // and the operator loses the known-good fallback. Additive value — a
  // companion that predates it should treat unknown reasons as a plain
  // failure, not crash.
  Unconfirmed = 8,
};

struct Hooks {
  /**
   * True when the running image has been confirmed (IMAGE_OK written).
   * Null means "don't care" — host tests that are not exercising the gate.
   */
  bool (*image_confirmed)(void* ctx) = nullptr;
  bool (*erase_slot)(void* ctx) = nullptr;
  bool (*write_slot)(std::uint32_t offset, const std::uint8_t* data,
                     std::size_t len, void* ctx) = nullptr;
  std::uint8_t (*battery_percent)(void* ctx) = nullptr;
  bool (*charging)(void* ctx) = nullptr;
  bool (*commit_ok)(std::uint32_t total, const std::uint8_t sha[32],
                    void* ctx) = nullptr;
  bool (*send)(const std::uint8_t* msg, std::size_t len, void* ctx) = nullptr;
  void* ctx = nullptr;
};

class Receiver {
public:
  void init(const Hooks& hooks);
  void on_message(const std::uint8_t* msg, std::size_t len);

  bool transfer_active() const { return active_; }
  std::uint16_t transfer_id() const { return id_; }
  std::uint32_t received() const { return received_; }
  std::uint32_t total() const { return total_; }
  std::uint16_t credit() const { return credit_; }

  // Sealed-hardware instrumentation (I-13): the watch has no SWD, so progress
  // and the last refusal have to be readable from the face.
  std::uint8_t progress_percent() const {
    if (total_ == 0u) return 0u;
    return static_cast<std::uint8_t>((received_ * 100u) / total_);
  }
  NakReason last_nak() const { return last_nak_; }
  std::uint32_t nak_count() const { return nak_count_; }
  /** True once a transfer has begun or been refused — gates the diag line. */
  bool touched() const { return touched_; }

private:
  void send_credit();
  void send_ack();
  void send_nak(NakReason reason);
  void reset();
  bool battery_ok() const;

  Hooks hooks_{};
  bool active_ = false;
  std::uint16_t id_ = 0u;
  std::uint32_t total_ = 0u;
  std::uint32_t received_ = 0u;
  std::uint16_t credit_ = kWindowBytes;
  std::uint8_t expect_sha_[32] = {};
  sha256::Ctx hash_{};
  bool hash_inited_ = false;
  NakReason last_nak_ = NakReason::Ok;
  std::uint32_t nak_count_ = 0u;
  bool touched_ = false;
};

}  // namespace ota
}  // namespace slate
