#include "asset_xfer.hpp"

#include <cstring>

namespace slate {
namespace asset {
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
  hash_ = 0u;
  received_ = 0u;
  credit_ = kWindowBytes;
  name_[0] = '\0';
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
  const std::uint8_t op = msg[0];
  switch (op) {
    case kOpBegin: {
      // BEGIN: id:u16, total:u32, hash:u32, name_len:u8, name...
      if (len < 12u) {
        send_nak(NakReason::BadMessage);
        return;
      }
      if (active_) {
        send_nak(NakReason::Busy);
        return;
      }
      id_ = rd_u16(msg + 1);
      total_ = rd_u32(msg + 3);
      hash_ = rd_u32(msg + 7);
      const std::uint8_t nlen = msg[11];
      if (nlen > kMaxNameLen || static_cast<std::size_t>(12u + nlen) > len) {
        send_nak(NakReason::BadMessage);
        return;
      }
      if (total_ == 0u || total_ > kMaxTransferBytes) {
        send_nak(NakReason::TooLarge);
        return;
      }
      std::memcpy(name_, msg + 12, nlen);
      name_[nlen] = '\0';
      received_ = 0u;
      credit_ = kWindowBytes;
      active_ = true;
      if (hooks_.abort_staging) {
        // Clear any prior staging.
        (void)hooks_.abort_staging(hooks_.ctx);
      }
      send_credit();
      send_ack();
      break;
    }
    case kOpChunk: {
      // CHUNK: id:u16, offset:u32, data...
      if (!active_ || len < 7u) {
        send_nak(NakReason::BadMessage);
        return;
      }
      const std::uint16_t id = rd_u16(msg + 1);
      const std::uint32_t off = rd_u32(msg + 3);
      if (id != id_) {
        send_nak(NakReason::BadMessage);
        return;
      }
      std::size_t data_len = len - 7u;
      if (yield_busy_ && data_len > kMaxBytesPerQuantum) {
        // Accept only the quantum; phone must retransmit remainder (resumable).
        data_len = kMaxBytesPerQuantum;
        send_nak(NakReason::Yield);
        // Still write the quantum so progress continues.
      }
      if (off != received_) {
        // Out of order — ask to resume at received_.
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
      if (hooks_.write_staging == nullptr ||
          !hooks_.write_staging(off, msg + 7, data_len, hooks_.ctx)) {
        send_nak(NakReason::NoStorage);
        reset();
        return;
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
      if (hooks_.abort_staging) {
        (void)hooks_.abort_staging(hooks_.ctx);
      }
      reset();
      send_credit();
      break;
    }
    case kOpCommit: {
      if (!active_ || len < 3u) {
        send_nak(NakReason::BadMessage);
        return;
      }
      if (rd_u16(msg + 1) != id_ || received_ != total_) {
        send_nak(NakReason::BadMessage);
        return;
      }
      // Hash verification is done by commit_staging (reads staging + FNV).
      if (hooks_.commit_staging == nullptr ||
          !hooks_.commit_staging(name_, total_, hash_, hooks_.ctx)) {
        send_nak(NakReason::HashFail);
        if (hooks_.abort_staging) {
          (void)hooks_.abort_staging(hooks_.ctx);
        }
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

}  // namespace asset
}  // namespace slate
