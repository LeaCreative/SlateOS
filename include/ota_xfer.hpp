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

constexpr std::uint16_t kWindowBytes = 2048u;
constexpr std::size_t kMaxBytesPerQuantum = 512u;

enum class NakReason : std::uint8_t {
  Ok = 0,
  Busy = 1,
  BadMessage = 2,
  HashFail = 3,
  TooLarge = 4,
  NoStorage = 5,
  LowBattery = 6,
  Yield = 7,
};

struct Hooks {
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
  std::uint16_t credit() const { return credit_; }

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
};

}  // namespace ota
}  // namespace slate
