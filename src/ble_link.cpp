#include "ble_link.hpp"

namespace ble {

void Link::init(bool diag_allowed) {
  diag_allowed_ = diag_allowed;
  reasm_.reset();
  reasm_.set_diag_allowed(diag_allowed);
  drops_ = 0u;
  loopbacks_ = 0u;
  for (auto& s : tx_seq_) {
    s = 0u;
  }
  status_ = StatusSnapshot{};
  status_.flags = diag_allowed ? static_cast<std::uint8_t>(1u << 1) : 0u;
}

void Link::set_tx(TxNotifyFn fn, void* ctx) {
  tx_ = fn;
  tx_ctx_ = ctx;
}

void Link::set_status(const StatusSnapshot& s) {
  status_ = s;
  if (diag_allowed_) {
    status_.flags = static_cast<std::uint8_t>(status_.flags | (1u << 1));
  } else {
    status_.flags = static_cast<std::uint8_t>(status_.flags & static_cast<std::uint8_t>(~(1u << 1)));
  }
}

bool Link::tx_emit(const std::uint8_t* pkt, std::size_t len, void* ctx) {
  auto* self = static_cast<Link*>(ctx);
  if (self == nullptr || self->tx_ == nullptr) {
    return false;
  }
  return self->tx_(pkt, len, self->tx_ctx_);
}

bool Link::send_message(std::uint8_t channel, const std::uint8_t* msg, std::size_t len) {
  if (channel >= sdp::frame::kChannelCount) {
    return false;
  }
  if (channel == sdp::frame::kChanDiag && !diag_allowed_) {
    return false;
  }
  return sdp::frame::fragment_message(
      channel, 0u, &tx_seq_[channel], msg, len, &Link::tx_emit, this);
}

void Link::on_rx_write(const std::uint8_t* data, std::size_t len) {
  const auto st = reasm_.ingest(data, len);
  switch (st) {
    case sdp::frame::FrameStatus::NeedMore:
      return;
    case sdp::frame::FrameStatus::Dropped:
    case sdp::frame::FrameStatus::ChannelReject:
      ++drops_;
      return;
    case sdp::frame::FrameStatus::Ok:
      break;
  }

  const std::uint8_t ch = reasm_.message_channel();
  const std::uint8_t* msg = reasm_.message();
  const std::size_t msg_len = reasm_.message_len();

  // M5: DIAG loopback only. Other channels accepted but not dispatched yet.
  if (ch == sdp::frame::kChanDiag && diag_allowed_) {
    if (send_message(sdp::frame::kChanDiag, msg, msg_len)) {
      ++loopbacks_;
    }
  }
}

}  // namespace ble
