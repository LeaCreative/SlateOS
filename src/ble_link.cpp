#include "ble_link.hpp"

namespace ble {

void Link::init(bool diag_allowed) {
  diag_allowed_ = diag_allowed;
  reasm_.reset();
  reasm_.set_diag_allowed(diag_allowed);
  inbox_.reset();
  drops_ = 0u;
  loopbacks_ = 0u;
  diag_bench_ = nullptr;
  app_ = nullptr;
  app_ctx_ = nullptr;
  wake_ = nullptr;
  wake_ctx_ = nullptr;
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

void Link::set_app_handler(AppMessageFn fn, void* ctx) {
  app_ = fn;
  app_ctx_ = ctx;
}

void Link::set_app_wake(AppWakeFn fn, void* ctx) {
  wake_ = fn;
  wake_ctx_ = ctx;
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

bool Link::reply_diag(const std::uint8_t* msg, std::size_t len) {
  if (send_message(sdp::frame::kChanDiag, msg, len)) {
    ++loopbacks_;
    return true;
  }
  return false;
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
  // Zero-copy handoff (N-8): the pending inbox message aliases reasm_'s
  // buffer, so while the app task hasn't drained it NOTHING may ingest —
  // including DIAG in debug — or the borrowed bytes would be overwritten.
  // CREDIT is withheld until the deferred apply completes, so a well-behaved
  // companion never sends into this window; a misbehaving one loses frames
  // (counted) and resyncs on its next FIRST.
  if (inbox_.busy()) {
    inbox_.note_busy_drop();
    return;
  }

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

  // DIAG stays on the producer context (bench / loopback latency). Everything
  // else must not touch session/interpreter/renderer here.
  if (ch == sdp::frame::kChanDiag && diag_allowed_) {
    if (diag_bench_ != nullptr) {
      (void)diag_bench_->handle(msg, msg_len);
    } else if (send_message(sdp::frame::kChanDiag, msg, msg_len)) {
      ++loopbacks_;
    }
    return;
  }

  // Borrow reasm_'s buffer until the app task drains (gate above keeps the
  // bytes stable). Only rejects malformed edge cases here — busy was gated.
  if (!inbox_.try_push(ch, msg, msg_len)) {
    return;
  }
  if (wake_ != nullptr) {
    wake_(wake_ctx_);
  }
}

bool Link::drain_app_messages() {
  if (app_ == nullptr) {
    return false;
  }
  return inbox_.drain_one(
      [this](std::uint8_t ch, const std::uint8_t* msg, std::size_t n) {
        app_(ch, msg, n, app_ctx_);
      });
}

}  // namespace ble
