#pragma once

#include <cstddef>
#include <cstdint>

// ASSET channel (3) receiver — chunked, resumable, hash-verified, windowed.
// Yields to DISPLAY/INPUT: process at most kMaxBytesPerQuantum per tick when
// `yield_busy` is set by the session/display path.

namespace slate {
namespace asset {

constexpr std::uint8_t kOpBegin = 0x01u;
constexpr std::uint8_t kOpChunk = 0x02u;
constexpr std::uint8_t kOpAbort = 0x03u;
constexpr std::uint8_t kOpCommit = 0x04u;

constexpr std::uint8_t kOpCredit = 0x10u;  // watch → phone
constexpr std::uint8_t kOpAck = 0x11u;
constexpr std::uint8_t kOpNak = 0x12u;

constexpr std::uint16_t kWindowBytes = 1024u;       // in-flight credit
constexpr std::size_t kMaxBytesPerQuantum = 256u;   // when yielding
constexpr std::size_t kMaxTransferBytes = 128u * 1024u;
constexpr std::size_t kMaxNameLen = 32u;

enum class NakReason : std::uint8_t {
  Ok = 0,
  Busy = 1,
  BadMessage = 2,
  HashFail = 3,
  TooLarge = 4,
  NoStorage = 5,
  Yield = 6,
};

struct Hooks {
  // Persist received bytes at offset within the staging file.
  bool (*write_staging)(std::uint32_t offset, const std::uint8_t* data,
                        std::size_t len, void* ctx) = nullptr;
  // Finalize staging → `/assets/<name>`; verify hash externally if needed.
  bool (*commit_staging)(const char* name, std::uint32_t total,
                         std::uint32_t hash, void* ctx) = nullptr;
  bool (*abort_staging)(void* ctx) = nullptr;
  // Send a complete ASSET message back to the phone.
  bool (*send)(const std::uint8_t* msg, std::size_t len, void* ctx) = nullptr;
  void* ctx = nullptr;
};

class Receiver {
public:
  void init(const Hooks& hooks);

  // When true, only accept/process up to kMaxBytesPerQuantum of CHUNK data
  // per on_message call (DISPLAY/INPUT contention).
  void set_yield_busy(bool busy) { yield_busy_ = busy; }

  void on_message(const std::uint8_t* msg, std::size_t len);

  // Advertised credit remaining in the current window.
  std::uint16_t credit() const { return credit_; }

  bool transfer_active() const { return active_; }
  std::uint16_t transfer_id() const { return id_; }
  std::uint32_t received() const { return received_; }

private:
  void send_credit();
  void send_ack();
  void send_nak(NakReason reason);
  void reset();

  Hooks hooks_{};
  bool yield_busy_ = false;
  bool active_ = false;
  std::uint16_t id_ = 0u;
  std::uint32_t total_ = 0u;
  std::uint32_t hash_ = 0u;
  std::uint32_t received_ = 0u;
  std::uint16_t credit_ = kWindowBytes;
  char name_[kMaxNameLen + 1] = {};
};

}  // namespace asset
}  // namespace slate
