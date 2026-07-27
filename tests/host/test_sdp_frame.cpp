#include "sdp_frame.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void expect_true(const char* name, bool cond) {
  if (!cond) {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

void expect_eq(const char* name, unsigned got, unsigned want) {
  if (got != want) {
    std::printf("FAIL %s: got %u want %u\n", name, got, want);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

struct Collect {
  std::vector<std::vector<std::uint8_t>> pkts;
};

bool collect_emit(const std::uint8_t* pkt, std::size_t len, void* ctx) {
  auto* c = static_cast<Collect*>(ctx);
  c->pkts.emplace_back(pkt, pkt + len);
  return true;
}

void test_single_fragment_roundtrip() {
  const std::uint8_t msg[] = {0x01, 0x02, 0x03, 0x04};
  Collect c;
  std::uint8_t seq = 0u;
  expect_true("frag single",
              sdp::frame::fragment_message(sdp::frame::kChanDisplay, 0, &seq,
                                           msg, sizeof(msg), collect_emit, &c));
  expect_eq("one pkt", static_cast<unsigned>(c.pkts.size()), 1u);
  expect_eq("seq advanced", seq, 1u);

  sdp::frame::Reassembler r;
  r.set_diag_allowed(true);
  auto st = r.ingest(c.pkts[0].data(), c.pkts[0].size());
  expect_eq("status Ok", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::Ok));
  expect_eq("len", static_cast<unsigned>(r.message_len()), 4u);
  expect_true("bytes",
              std::memcmp(r.message(), msg, 4) == 0);
}

void test_fragmentation() {
  std::vector<std::uint8_t> msg(500);
  for (std::size_t i = 0; i < msg.size(); ++i) {
    msg[i] = static_cast<std::uint8_t>(i & 0xFF);
  }
  Collect c;
  std::uint8_t seq = 10u;
  expect_true("frag multi",
              sdp::frame::fragment_message(sdp::frame::kChanDisplay, 0, &seq,
                                           msg.data(), msg.size(),
                                           collect_emit, &c));
  expect_true(">=2 pkts", c.pkts.size() >= 2u);

  // First fragment must carry total length.
  expect_true("first has len", c.pkts[0].size() >= 4u);
  const std::uint16_t total =
      static_cast<std::uint16_t>(c.pkts[0][2] | (c.pkts[0][3] << 8));
  expect_eq("total len", total, 500u);

  sdp::frame::Reassembler r;
  sdp::frame::FrameStatus last = sdp::frame::FrameStatus::Dropped;
  for (const auto& p : c.pkts) {
    last = r.ingest(p.data(), p.size());
  }
  expect_eq("reassembled Ok", static_cast<unsigned>(last),
            static_cast<unsigned>(sdp::frame::FrameStatus::Ok));
  expect_eq("reassembled len", static_cast<unsigned>(r.message_len()), 500u);
  expect_true("payload match",
              std::memcmp(r.message(), msg.data(), msg.size()) == 0);
}

void test_sequence_wrap() {
  const std::uint8_t msg[] = {0xAA};
  Collect c;
  std::uint8_t seq = 255u;
  expect_true("frag at 255",
              sdp::frame::fragment_message(1, 0, &seq, msg, 1, collect_emit, &c));
  expect_eq("wrapped to 0", seq, 0u);
  expect_eq("pkt seq byte", c.pkts[0][1], 255u);

  // Multi-fragment starting at 254 so second wraps.
  std::vector<std::uint8_t> big(400, 0x55);
  c.pkts.clear();
  seq = 254u;
  expect_true("frag wrap multi",
              sdp::frame::fragment_message(1, 0, &seq, big.data(), big.size(),
                                           collect_emit, &c));
  expect_true("multi pkts", c.pkts.size() >= 2u);
  expect_eq("first seq 254", c.pkts[0][1], 254u);
  expect_eq("second seq 255", c.pkts[1][1], 255u);
  if (c.pkts.size() >= 3u) {
    expect_eq("third seq 0", c.pkts[2][1], 0u);
  }

  sdp::frame::Reassembler r;
  sdp::frame::FrameStatus last = sdp::frame::FrameStatus::Dropped;
  for (const auto& p : c.pkts) {
    last = r.ingest(p.data(), p.size());
  }
  expect_eq("wrap reasm Ok", static_cast<unsigned>(last),
            static_cast<unsigned>(sdp::frame::FrameStatus::Ok));
}

void test_out_of_order() {
  std::vector<std::uint8_t> msg(400, 0x11);
  Collect c;
  std::uint8_t seq = 0u;
  sdp::frame::fragment_message(1, 0, &seq, msg.data(), msg.size(),
                               collect_emit, &c);
  expect_true("need >=2", c.pkts.size() >= 2u);

  sdp::frame::Reassembler r;
  auto st0 = r.ingest(c.pkts[0].data(), c.pkts[0].size());
  expect_eq("first NeedMore", static_cast<unsigned>(st0),
            static_cast<unsigned>(sdp::frame::FrameStatus::NeedMore));

  // Skip packet 1, feed packet 2 (wrong seq).
  if (c.pkts.size() >= 3u) {
    auto st = r.ingest(c.pkts[2].data(), c.pkts[2].size());
    expect_eq("ooo Dropped", static_cast<unsigned>(st),
              static_cast<unsigned>(sdp::frame::FrameStatus::Dropped));
  } else {
    // Only 2 packets: feed first again (wrong — expect seq 1, got 0).
    auto st = r.ingest(c.pkts[0].data(), c.pkts[0].size());
    // First flag starts new message — that's OK. Feed second with wrong by
    // corrupting seq.
    auto bad = c.pkts[1];
    bad[1] = static_cast<std::uint8_t>(bad[1] + 5u);
    st = r.ingest(c.pkts[0].data(), c.pkts[0].size());
    (void)st;
    st = r.ingest(bad.data(), bad.size());
    expect_eq("corrupt seq Dropped", static_cast<unsigned>(st),
              static_cast<unsigned>(sdp::frame::FrameStatus::Dropped));
  }
}

void test_truncated_frame() {
  sdp::frame::Reassembler r;
  std::uint8_t one[] = {0x20};  // only byte0, missing seq
  auto st = r.ingest(one, 1);
  expect_eq("trunc Dropped", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::Dropped));

  // FIRST without LAST but missing length bytes.
  std::uint8_t short_first[] = {
      sdp::frame::pack_byte0(1, sdp::frame::kFlagFirst), 0x00};
  st = r.ingest(short_first, 2);
  expect_eq("first no len Dropped", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::Dropped));
}

void test_oversized_length() {
  // FIRST, !LAST, total_len = 5000 > 4096.
  std::uint8_t pkt[8] = {};
  pkt[0] = sdp::frame::pack_byte0(1, sdp::frame::kFlagFirst);
  pkt[1] = 0x00;
  pkt[2] = static_cast<std::uint8_t>(5000 & 0xFF);
  pkt[3] = static_cast<std::uint8_t>((5000 >> 8) & 0xFF);
  pkt[4] = 0xDE;

  sdp::frame::Reassembler r;
  auto st = r.ingest(pkt, 5);
  expect_eq("oversize Dropped", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::Dropped));
}

void test_diag_channel_release() {
  const std::uint8_t msg[] = {0x01};
  Collect c;
  std::uint8_t seq = 0u;
  sdp::frame::fragment_message(sdp::frame::kChanDiag, 0, &seq, msg, 1,
                               collect_emit, &c);

  sdp::frame::Reassembler r;
  r.set_diag_allowed(false);
  auto st = r.ingest(c.pkts[0].data(), c.pkts[0].size());
  expect_eq("diag reject", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::ChannelReject));
}

void test_resync_after_drop() {
  std::vector<std::uint8_t> msg(400, 0x22);
  Collect c;
  std::uint8_t seq = 0u;
  sdp::frame::fragment_message(1, 0, &seq, msg.data(), msg.size(),
                               collect_emit, &c);

  sdp::frame::Reassembler r;
  (void)r.ingest(c.pkts[0].data(), c.pkts[0].size());
  auto bad = c.pkts[1];
  bad[1] = 0x99;  // wrong seq
  auto st = r.ingest(bad.data(), bad.size());
  expect_eq("drop", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::Dropped));

  // Fresh good message must still work (channel resynced).
  Collect c2;
  seq = 0u;
  const std::uint8_t small[] = {0x42};
  sdp::frame::fragment_message(1, 0, &seq, small, 1, collect_emit, &c2);
  st = r.ingest(c2.pkts[0].data(), c2.pkts[0].size());
  expect_eq("resync Ok", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::Ok));
  expect_eq("byte", r.message()[0], 0x42u);
}

}  // namespace

int main() {
  test_single_fragment_roundtrip();
  test_fragmentation();
  test_sequence_wrap();
  test_out_of_order();
  test_truncated_frame();
  test_oversized_length();
  test_diag_channel_release();
  test_resync_after_drop();
  if (g_failures != 0) {
    std::printf("%d FAILURES\n", g_failures);
    return 1;
  }
  std::printf("all framing tests passed\n");
  return 0;
}
