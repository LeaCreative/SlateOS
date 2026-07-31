#include "sdp_frame.hpp"

#include <cstring>

namespace sdp {
namespace frame {

bool fragment_message(
    std::uint8_t channel,
    std::uint8_t extra_flags,
    std::uint8_t* seq_io,
    const std::uint8_t* msg,
    std::size_t msg_len,
    FragmentEmit emit,
    void* ctx) {
  if (seq_io == nullptr || emit == nullptr) {
    return false;
  }
  if (msg_len > kMaxMessageBytes) {
    return false;
  }
  if (msg_len > 0u && msg == nullptr) {
    return false;
  }

  const std::uint8_t extra = static_cast<std::uint8_t>(extra_flags & 0x1Cu);
  constexpr std::size_t kSingleCap = kAttPayloadMax - 2u;  // FIRST|LAST
  constexpr std::size_t kFirstMultiCap = kAttPayloadMax - 4u;
  constexpr std::size_t kContCap = kAttPayloadMax - 2u;

  if (msg_len <= kSingleCap) {
    std::uint8_t pkt[kAttPayloadMax];
    const std::uint8_t flags =
        static_cast<std::uint8_t>(kFlagFirst | kFlagLast | extra);
    const std::size_t n = encode_fragment(
        channel, flags, *seq_io, static_cast<std::uint16_t>(msg_len),
        msg, msg_len, pkt, sizeof(pkt));
    if (n == 0u) return false;
    *seq_io = static_cast<std::uint8_t>(*seq_io + 1u);
    return emit(pkt, n, ctx);
  }

  // Multi-fragment.
  std::size_t offset = 0u;
  bool is_first = true;
  while (offset < msg_len) {
    std::uint8_t flags = extra;
    std::size_t cap = kContCap;
    if (is_first) {
      flags = static_cast<std::uint8_t>(flags | kFlagFirst);
      cap = kFirstMultiCap;
    }
    std::size_t chunk = msg_len - offset;
    if (chunk > cap) {
      chunk = cap;
    }
    if (offset + chunk >= msg_len) {
      flags = static_cast<std::uint8_t>(flags | kFlagLast);
    }

    std::uint8_t pkt[kAttPayloadMax];
    const std::size_t n = encode_fragment(
        channel, flags, *seq_io, static_cast<std::uint16_t>(msg_len),
        msg + offset, chunk, pkt, sizeof(pkt));
    if (n == 0u) return false;
    *seq_io = static_cast<std::uint8_t>(*seq_io + 1u);
    if (!emit(pkt, n, ctx)) return false;
    offset += chunk;
    is_first = false;
  }
  return true;
}

std::size_t encode_fragment(
    std::uint8_t channel,
    std::uint8_t flags,
    std::uint8_t seq,
    std::uint16_t total_len,
    const std::uint8_t* payload,
    std::size_t payload_len,
    std::uint8_t* out,
    std::size_t out_cap) {
  if (out == nullptr || channel >= kChannelCount) {
    return 0u;
  }
  if ((flags & 0x10u) != 0u) {
    return 0u;  // reserved bit must be zero
  }

  const bool first = (flags & kFlagFirst) != 0u;
  const bool last  = (flags & kFlagLast) != 0u;
  const bool need_len = first && !last;
  const std::size_t hdr = need_len ? 4u : 2u;

  if (out_cap < hdr + payload_len) {
    return 0u;
  }
  if (hdr + payload_len > kAttPayloadMax) {
    return 0u;
  }
  if (payload_len > 0u && payload == nullptr) {
    return 0u;
  }

  out[0] = pack_byte0(channel, flags);
  out[1] = seq;
  if (need_len) {
    out[2] = static_cast<std::uint8_t>(total_len & 0xFFu);
    out[3] = static_cast<std::uint8_t>((total_len >> 8) & 0xFFu);
  }
  if (payload_len > 0u) {
    std::memcpy(out + hdr, payload, payload_len);
  }
  return hdr + payload_len;
}

void Reassembler::reset() {
  for (std::uint8_t i = 0u; i < kChannelCount; ++i) {
    reset_channel(i);
  }
  msg_len_ = 0u;
  msg_channel_ = 0u;
  preempt_drops_ = 0u;
}

void Reassembler::reset_channel(std::uint8_t channel) {
  if (channel >= kChannelCount) {
    return;
  }
  ch_[channel] = ChanState{};
}

FrameStatus Reassembler::ingest(const std::uint8_t* pkt, std::size_t len) {
  msg_len_ = 0u;

  if (pkt == nullptr || len < 2u || len > kAttPayloadMax) {
    return FrameStatus::Dropped;
  }

  std::uint8_t channel = 0u;
  std::uint8_t flags = 0u;
  unpack_byte0(pkt[0], &channel, &flags);
  const std::uint8_t seq = pkt[1];

  // Reserved flag bit4 must be zero.
  if ((flags & 0x10u) != 0u) {
    reset_channel(channel);
    return FrameStatus::Dropped;
  }

  if (channel == kChanReserved) {
    return FrameStatus::ChannelReject;
  }
  if (channel == kChanDiag && !diag_allowed_) {
    return FrameStatus::ChannelReject;
  }

  const bool first = (flags & kFlagFirst) != 0u;
  const bool last  = (flags & kFlagLast) != 0u;
  const bool need_len = first && !last;

  if (need_len && len < 4u) {
    reset_channel(channel);
    return FrameStatus::Dropped;
  }

  const std::size_t hdr = need_len ? 4u : 2u;
  const std::uint8_t* payload = pkt + hdr;
  const std::size_t payload_len = len - hdr;

  ChanState& st = ch_[channel];

  if (first) {
    // Single-in-flight: buf_ is shared across channels, so a FIRST here
    // aborts any in-flight reassembly on *other* channels before it can be
    // silently corrupted. Protocol violation by the sender — resync to the
    // newest message and count the abandoned one (reject-and-resync).
    for (std::uint8_t i = 0u; i < kChannelCount; ++i) {
      if (i != channel && ch_[i].active) {
        reset_channel(i);
        ++preempt_drops_;
      }
    }

    // New message — abandon any in-progress reassembly on this channel.
    st = ChanState{};
    st.saw_first = true;
    st.expect_seq = static_cast<std::uint8_t>(seq + 1u);
    st.active = !last;

    if (need_len) {
      st.total_len = static_cast<std::uint16_t>(
          pkt[2] | (static_cast<std::uint16_t>(pkt[3]) << 8));
      if (st.total_len == 0u || st.total_len > kMaxMessageBytes) {
        reset_channel(channel);
        return FrameStatus::Dropped;
      }
    } else if (last) {
      // Single-fragment message.
      st.total_len = static_cast<std::uint16_t>(payload_len);
    } else {
      // FIRST without LAST must carry length.
      reset_channel(channel);
      return FrameStatus::Dropped;
    }

    if (payload_len > st.total_len) {
      reset_channel(channel);
      return FrameStatus::Dropped;
    }

    std::memcpy(buf_, payload, payload_len);
    st.filled = static_cast<std::uint16_t>(payload_len);

    if (last) {
      if (st.filled != st.total_len) {
        reset_channel(channel);
        return FrameStatus::Dropped;
      }
      msg_len_ = st.filled;
      msg_channel_ = channel;
      reset_channel(channel);
      return FrameStatus::Ok;
    }
    return FrameStatus::NeedMore;
  }

  // Continuation fragment.
  if (!st.active || !st.saw_first) {
    reset_channel(channel);
    return FrameStatus::Dropped;
  }
  if (seq != st.expect_seq) {
    reset_channel(channel);
    return FrameStatus::Dropped;
  }
  st.expect_seq = static_cast<std::uint8_t>(st.expect_seq + 1u);

  if (static_cast<std::uint32_t>(st.filled) + payload_len > st.total_len) {
    reset_channel(channel);
    return FrameStatus::Dropped;
  }

  std::memcpy(buf_ + st.filled, payload, payload_len);
  st.filled = static_cast<std::uint16_t>(st.filled + payload_len);

  if (last) {
    if (st.filled != st.total_len) {
      reset_channel(channel);
      return FrameStatus::Dropped;
    }
    msg_len_ = st.filled;
    msg_channel_ = channel;
    reset_channel(channel);
    return FrameStatus::Ok;
  }

  if (st.filled == st.total_len) {
    // Filled early without LAST — malformed.
    reset_channel(channel);
    return FrameStatus::Dropped;
  }
  return FrameStatus::NeedMore;
}

}  // namespace frame
}  // namespace sdp
