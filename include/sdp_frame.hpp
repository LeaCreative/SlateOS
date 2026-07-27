#pragma once

#include <cstddef>
#include <cstdint>

// SDP packet framing — §4.2. Host-safe (no firmware deps).
// ATT payload at MTU 247 is 244 bytes; frame header is 2 or 4 bytes.

namespace sdp {
namespace frame {

constexpr std::uint16_t kAttMtuTarget     = 247u;
constexpr std::uint16_t kAttPayloadMax    = 244u;  // MTU − 3
constexpr std::uint16_t kMaxMessageBytes  = 4096u;
constexpr std::uint8_t  kChannelCount     = 8u;

constexpr std::uint8_t kChanControl = 0u;
constexpr std::uint8_t kChanDisplay = 1u;
constexpr std::uint8_t kChanInput   = 2u;
constexpr std::uint8_t kChanAsset   = 3u;
constexpr std::uint8_t kChanSystem  = 4u;
constexpr std::uint8_t kChanOta     = 5u;
constexpr std::uint8_t kChanReserved = 6u;
constexpr std::uint8_t kChanDiag    = 7u;

// FLAGS in byte 0 bits 0–4 (CHAN is bits 5–7).
constexpr std::uint8_t kFlagFirst  = 1u << 0;
constexpr std::uint8_t kFlagLast   = 1u << 1;
constexpr std::uint8_t kFlagAckReq = 1u << 2;
constexpr std::uint8_t kFlagUrgent = 1u << 3;
// bit4 reserved

enum class FrameStatus : std::uint8_t {
  Ok = 0,
  NeedMore,       // fragment accepted; message not complete
  Dropped,        // malformed / out-of-order / overflow — channel reset
  ChannelReject,  // channel not allowed (e.g. diag in release)
};

struct Header {
  std::uint8_t channel = 0u;
  std::uint8_t flags   = 0u;
  std::uint8_t seq     = 0u;
  bool has_total_len   = false;
  std::uint16_t total_len = 0u;
};

inline std::uint8_t pack_byte0(std::uint8_t channel, std::uint8_t flags) {
  return static_cast<std::uint8_t>(((channel & 0x07u) << 5) | (flags & 0x1Fu));
}

inline void unpack_byte0(std::uint8_t b0, std::uint8_t* channel, std::uint8_t* flags) {
  *channel = static_cast<std::uint8_t>((b0 >> 5) & 0x07u);
  *flags   = static_cast<std::uint8_t>(b0 & 0x1Fu);
}

// Encode one ATT payload fragment into `out` (capacity >= kAttPayloadMax).
// Returns bytes written, or 0 on error.
std::size_t encode_fragment(
    std::uint8_t channel,
    std::uint8_t flags,
    std::uint8_t seq,
    std::uint16_t total_len,          // only used when FIRST && !LAST
    const std::uint8_t* payload,
    std::size_t payload_len,
    std::uint8_t* out,
    std::size_t out_cap);

// Callback for each encoded ATT fragment. Return false to abort.
using FragmentEmit = bool (*)(const std::uint8_t* pkt, std::size_t len, void* ctx);

// Fragment an entire message into ATT-sized frames.
// `extra_flags`: ACK_REQ / URGENT only (FIRST/LAST applied here).
// `seq_io` incremented once per fragment (wraps at 256).
bool fragment_message(
    std::uint8_t channel,
    std::uint8_t extra_flags,
    std::uint8_t* seq_io,
    const std::uint8_t* msg,
    std::size_t msg_len,
    FragmentEmit emit,
    void* ctx);

// Per-channel reassembly. On Dropped, the channel state is reset (resync).
class Reassembler {
public:
  void reset();
  void reset_channel(std::uint8_t channel);

  // Allow/deny channels 6–7 (release must deny).
  void set_diag_allowed(bool allowed) { diag_allowed_ = allowed; }

  FrameStatus ingest(const std::uint8_t* pkt, std::size_t len);

  // After Ok: message is complete. Valid until next ingest/reset_channel.
  const std::uint8_t* message() const { return buf_; }
  std::size_t message_len() const { return msg_len_; }
  std::uint8_t message_channel() const { return msg_channel_; }

private:
  struct ChanState {
    bool active = false;
    std::uint8_t expect_seq = 0u;
    std::uint16_t total_len = 0u;
    std::uint16_t filled = 0u;
    bool saw_first = false;
  };

  ChanState ch_[kChannelCount] = {};
  std::uint8_t buf_[kMaxMessageBytes] = {};
  std::size_t msg_len_ = 0u;
  std::uint8_t msg_channel_ = 0u;
  bool diag_allowed_ = true;  // debug default; release sets false
};

}  // namespace frame
}  // namespace sdp
