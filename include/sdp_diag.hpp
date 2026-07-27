#pragma once

#include <cstddef>
#include <cstdint>

// Channel-7 DIAG benchmark protocol (debug builds only).
// Phone → watch: request opcodes 0x01–0x06.
// Watch → phone: response opcodes 0x81–0x86 (request | 0x80).

namespace sdp {
namespace diag {

constexpr std::uint8_t kOpPingReq     = 0x01;
constexpr std::uint8_t kOpThruStart   = 0x02;
constexpr std::uint8_t kOpThruData    = 0x03;
constexpr std::uint8_t kOpRttReq      = 0x04;
constexpr std::uint8_t kOpMbufReq     = 0x05;
constexpr std::uint8_t kOpRenderReq   = 0x06;

constexpr std::uint8_t kOpPingRsp     = 0x81;
constexpr std::uint8_t kOpThruResult  = 0x82;
constexpr std::uint8_t kOpRttRsp      = 0x84;
constexpr std::uint8_t kOpMbufRsp     = 0x85;
constexpr std::uint8_t kOpRenderRsp   = 0x86;

constexpr std::uint8_t kRspBit = 0x80;

struct MbufReport {
  std::uint16_t peak_used = 0u;
  std::uint16_t block_count = 0u;
  std::uint16_t block_size = 0u;
  std::uint16_t free_now = 0u;
};

struct RenderReport {
  std::uint32_t parse_us = 0u;
  std::uint32_t render_us = 0u;
  std::uint32_t ops = 0u;
  std::uint8_t status = 0u;  // sdp::Status
};

using NowUsFn = std::uint32_t (*)();
using ReplyFn = bool (*)(const std::uint8_t* msg, std::size_t len, void* ctx);
using RenderFn = RenderReport (*)(const std::uint8_t* dl, std::size_t len, void* ctx);
using MbufFn = MbufReport (*)(void* ctx);

class Bench {
public:
  void init(NowUsFn now_us, ReplyFn reply, void* reply_ctx,
            RenderFn render, void* render_ctx,
            MbufFn mbuf, void* mbuf_ctx);

  // Handle a completed DIAG message. Returns true if handled.
  bool handle(const std::uint8_t* msg, std::size_t len);

  std::uint32_t thru_bytes() const { return thru_got_; }
  bool thru_active() const { return thru_active_; }

private:
  bool reply_bytes(const std::uint8_t* msg, std::size_t len);
  void handle_ping(const std::uint8_t* msg, std::size_t len);
  void handle_thru_start(const std::uint8_t* msg, std::size_t len);
  void handle_thru_data(const std::uint8_t* msg, std::size_t len);
  void maybe_finish_thru();
  void handle_rtt(const std::uint8_t* msg, std::size_t len);
  void handle_mbuf();
  void handle_render(const std::uint8_t* msg, std::size_t len);

  NowUsFn now_us_ = nullptr;
  ReplyFn reply_ = nullptr;
  void* reply_ctx_ = nullptr;
  RenderFn render_ = nullptr;
  void* render_ctx_ = nullptr;
  MbufFn mbuf_ = nullptr;
  void* mbuf_ctx_ = nullptr;

  bool thru_active_ = false;
  std::uint32_t thru_expect_ = 0u;
  std::uint32_t thru_got_ = 0u;
  std::uint32_t thru_t0_us_ = 0u;
};

}  // namespace diag
}  // namespace sdp
