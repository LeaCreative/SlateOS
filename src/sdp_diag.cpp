#include "sdp_diag.hpp"

#include <cstring>

namespace sdp {
namespace diag {
namespace {

void put_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void put_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::uint32_t get_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

bool is_req_op(std::uint8_t op) {
  return op == kOpPingReq || op == kOpThruStart || op == kOpThruData ||
         op == kOpRttReq || op == kOpMbufReq || op == kOpRenderReq;
}

}  // namespace

void Bench::init(NowUsFn now_us, ReplyFn reply, void* reply_ctx,
                 RenderFn render, void* render_ctx,
                 MbufFn mbuf, void* mbuf_ctx) {
  now_us_ = now_us;
  reply_ = reply;
  reply_ctx_ = reply_ctx;
  render_ = render;
  render_ctx_ = render_ctx;
  mbuf_ = mbuf;
  mbuf_ctx_ = mbuf_ctx;
  thru_active_ = false;
  thru_expect_ = 0u;
  thru_got_ = 0u;
  thru_t0_us_ = 0u;
}

bool Bench::reply_bytes(const std::uint8_t* msg, std::size_t len) {
  if (reply_ == nullptr) {
    return false;
  }
  return reply_(msg, len, reply_ctx_);
}

bool Bench::handle(const std::uint8_t* msg, std::size_t len) {
  if (msg == nullptr || len == 0u) {
    return false;
  }
  const std::uint8_t op = msg[0];
  if (!is_req_op(op)) {
    // Legacy M5 loopback: echo opaque payload unchanged.
    return reply_bytes(msg, len);
  }
  switch (op) {
    case kOpPingReq:
      handle_ping(msg, len);
      return true;
    case kOpThruStart:
      handle_thru_start(msg, len);
      return true;
    case kOpThruData:
      handle_thru_data(msg, len);
      return true;
    case kOpRttReq:
      handle_rtt(msg, len);
      return true;
    case kOpMbufReq:
      handle_mbuf();
      return true;
    case kOpRenderReq:
      handle_render(msg, len);
      return true;
    default:
      return false;
  }
}

void Bench::handle_ping(const std::uint8_t* msg, std::size_t len) {
  // Echo token with response opcode; keep payload bytes 1..n.
  if (len > 256u) {
    len = 256u;
  }
  std::uint8_t out[256];
  out[0] = kOpPingRsp;
  if (len > 1u) {
    std::memcpy(out + 1, msg + 1, len - 1u);
  }
  reply_bytes(out, len);
}

void Bench::handle_thru_start(const std::uint8_t* msg, std::size_t len) {
  if (len < 5u || now_us_ == nullptr) {
    return;
  }
  thru_expect_ = get_u32(msg + 1);
  thru_got_ = 0u;
  thru_t0_us_ = now_us_();
  thru_active_ = thru_expect_ > 0u;

  // Optional first chunk after the header.
  if (len > 5u && thru_active_) {
    const std::size_t chunk = len - 5u;
    thru_got_ += static_cast<std::uint32_t>(chunk);
    maybe_finish_thru();
  }
}

void Bench::handle_thru_data(const std::uint8_t* msg, std::size_t len) {
  (void)msg;
  if (!thru_active_ || len < 1u) {
    return;
  }
  const std::size_t chunk = len - 1u;
  thru_got_ += static_cast<std::uint32_t>(chunk);
  maybe_finish_thru();
}

void Bench::maybe_finish_thru() {
  if (!thru_active_ || thru_got_ < thru_expect_ || now_us_ == nullptr) {
    return;
  }
  const std::uint32_t elapsed = now_us_() - thru_t0_us_;
  thru_active_ = false;

  // kbps_x100 = (bytes * 100000) / elapsed_us  →  centi-kB/s
  // (bytes / (elapsed_us/1e6)) / 1000 * 100 = bytes * 100 / (elapsed_us/1000)
  //             = bytes * 1e5 / elapsed_us
  std::uint32_t kbps_x100 = 0u;
  if (elapsed > 0u) {
    const std::uint64_t num =
        static_cast<std::uint64_t>(thru_got_) * 100000ull;
    kbps_x100 = static_cast<std::uint32_t>(num / elapsed);
  }

  std::uint8_t out[14];
  out[0] = kOpThruResult;
  put_u32(out + 1, elapsed);
  put_u32(out + 5, thru_got_);
  put_u32(out + 9, kbps_x100);
  out[13] = 0u;  // status ok
  reply_bytes(out, sizeof(out));
}

void Bench::handle_rtt(const std::uint8_t* msg, std::size_t len) {
  // Echo phone token (bytes 1..) and append watch receive timestamp (u32 us).
  if (len < 2u || now_us_ == nullptr) {
    return;
  }
  const std::size_t token_len = len - 1u;
  if (token_len > 64u) {
    return;
  }
  std::uint8_t out[1 + 64 + 4];
  out[0] = kOpRttRsp;
  std::memcpy(out + 1, msg + 1, token_len);
  put_u32(out + 1 + token_len, now_us_());
  reply_bytes(out, 1u + token_len + 4u);
}

void Bench::handle_mbuf() {
  MbufReport r{};
  if (mbuf_ != nullptr) {
    r = mbuf_(mbuf_ctx_);
  }
  std::uint8_t out[9];
  out[0] = kOpMbufRsp;
  put_u16(out + 1, r.peak_used);
  put_u16(out + 3, r.block_count);
  put_u16(out + 5, r.block_size);
  put_u16(out + 7, r.free_now);
  reply_bytes(out, sizeof(out));
}

void Bench::handle_render(const std::uint8_t* msg, std::size_t len) {
  RenderReport stats{};
  if (len > 1u && render_ != nullptr) {
    stats = render_(msg + 1, len - 1u, render_ctx_);
  } else {
    stats.status = 1u;  // Truncated
  }
  std::uint8_t out[14];
  out[0] = kOpRenderRsp;
  put_u32(out + 1, stats.parse_us);
  put_u32(out + 5, stats.render_us);
  put_u32(out + 9, stats.ops);
  out[13] = stats.status;
  reply_bytes(out, sizeof(out));
}

}  // namespace diag
}  // namespace sdp
