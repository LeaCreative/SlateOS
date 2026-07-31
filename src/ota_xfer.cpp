#include "ota_xfer.hpp"

#include "flash_map.hpp"

#include <cstring>

namespace slate {
namespace ota {
namespace {

std::uint16_t rd_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t rd_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
void wr_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}
void wr_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

}  // namespace

void Receiver::init(const Hooks& hooks) {
  hooks_ = hooks;
  reset();
  send_credit();
}

void Receiver::reset() {
  active_ = false;
  id_ = 0u;
  total_ = 0u;
  received_ = 0u;
  credit_ = kWindowBytes;
  std::memset(expect_sha_, 0, sizeof(expect_sha_));
  hash_inited_ = false;
}

bool Receiver::battery_ok() const {
  if (hooks_.battery_percent == nullptr) {
    return true;  // host tests without battery
  }
  if (hooks_.charging && hooks_.charging(hooks_.ctx)) {
    return true;
  }
  const std::uint8_t p = hooks_.battery_percent(hooks_.ctx);
  // kPercentUnknown (0xFF) must block — not pass as "≥30".
  if (p > 100u) {
    return false;
  }
  return p >= flash_map::kOtaMinBatteryPercent;
}

void Receiver::send_credit() {
  if (hooks_.send == nullptr) {
    return;
  }
  std::uint8_t msg[3] = {kOpCredit};
  wr_u16(msg + 1, credit_);
  (void)hooks_.send(msg, sizeof(msg), hooks_.ctx);
}

void Receiver::send_ack() {
  if (hooks_.send == nullptr) {
    return;
  }
  std::uint8_t msg[7] = {kOpAck};
  wr_u16(msg + 1, id_);
  wr_u32(msg + 3, received_);
  (void)hooks_.send(msg, sizeof(msg), hooks_.ctx);
}

void Receiver::send_nak(NakReason reason) {
  if (hooks_.send == nullptr) {
    return;
  }
  const std::uint8_t msg[2] = {kOpNak, static_cast<std::uint8_t>(reason)};
  (void)hooks_.send(msg, sizeof(msg), hooks_.ctx);
}

void Receiver::on_message(const std::uint8_t* msg, std::size_t len) {
  if (msg == nullptr || len == 0u) {
    return;
  }
  switch (msg[0]) {
    case kOpBegin: {
      // BEGIN: id:u16, total:u32, sha256[32], version:u32  (+ op)
      if (len < 43u) {
        send_nak(NakReason::BadMessage);
        return;
      }
      if (!battery_ok()) {
        send_nak(NakReason::LowBattery);
        return;
      }
      const std::uint16_t id = rd_u16(msg + 1);
      const std::uint32_t total = rd_u32(msg + 3);
      const std::uint8_t* sha = msg + 7;
      // version at msg+39 — reserved for logging / future checks
      (void)rd_u32(msg + 39);

      if (total == 0u || total > flash_map::kSecondarySize) {
        send_nak(NakReason::TooLarge);
        return;
      }

      // Resume: same id, already active — ACK current offset.
      if (active_ && id == id_) {
        send_ack();
        send_credit();
        return;
      }
      if (active_) {
        send_nak(NakReason::Busy);
        return;
      }

      if (hooks_.erase_slot == nullptr || !hooks_.erase_slot(hooks_.ctx)) {
        send_nak(NakReason::NoStorage);
        return;
      }

      id_ = id;
      total_ = total;
      received_ = 0u;
      credit_ = kWindowBytes;
      std::memcpy(expect_sha_, sha, 32u);
      sha256::init(hash_);
      hash_inited_ = true;
      active_ = true;
      send_credit();
      send_ack();
      break;
    }
    case kOpChunk: {
      if (!active_ || len < 7u) {
        send_nak(NakReason::BadMessage);
        return;
      }
      if (rd_u16(msg + 1) != id_) {
        send_nak(NakReason::BadMessage);
        return;
      }
      const std::uint32_t off = rd_u32(msg + 3);
      std::size_t data_len = len - 7u;
      if (data_len > kMaxBytesPerQuantum) {
        data_len = kMaxBytesPerQuantum;
        send_nak(NakReason::Yield);
      }
      if (off != received_) {
        send_ack();
        return;
      }
      if (received_ + data_len > total_) {
        send_nak(NakReason::BadMessage);
        return;
      }
      if (data_len > credit_) {
        send_nak(NakReason::Busy);
        return;
      }
      if (hooks_.write_slot == nullptr ||
          !hooks_.write_slot(off, msg + 7, data_len, hooks_.ctx)) {
        send_nak(NakReason::NoStorage);
        reset();
        return;
      }
      if (hash_inited_) {
        sha256::update(hash_, msg + 7, data_len);
      }
      received_ += static_cast<std::uint32_t>(data_len);
      credit_ = static_cast<std::uint16_t>(credit_ - data_len);
      if (credit_ < kWindowBytes / 4u) {
        credit_ = kWindowBytes;
        send_credit();
      }
      send_ack();
      break;
    }
    case kOpAbort: {
      reset();
      send_credit();
      break;
    }
    case kOpCommit: {
      if (!active_ || len < 3u) {
        send_nak(NakReason::BadMessage);
        return;
      }
      if (rd_u16(msg + 1) != id_ || received_ != total_ || !hash_inited_) {
        send_nak(NakReason::BadMessage);
        return;
      }
      std::uint8_t got[32];
      sha256::final(hash_, got);
      if (std::memcmp(got, expect_sha_, 32u) != 0) {
        send_nak(NakReason::HashFail);
        reset();
        return;
      }
      if (hooks_.commit_ok == nullptr ||
          !hooks_.commit_ok(total_, expect_sha_, hooks_.ctx)) {
        send_nak(NakReason::NoStorage);
        reset();
        return;
      }
      reset();
      send_credit();
      break;
    }
    default:
      send_nak(NakReason::BadMessage);
      break;
  }
}

}  // namespace ota
}  // namespace slate
