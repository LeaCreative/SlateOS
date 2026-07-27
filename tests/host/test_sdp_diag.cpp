#include "ble_link.hpp"
#include "ble_mbuf_stats.hpp"
#include "sdp_diag.hpp"
#include "sdp_frame.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;
std::uint32_t g_fake_us = 1000u;

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

std::uint32_t fake_now() { return g_fake_us; }

struct TxCap {
  std::vector<std::vector<std::uint8_t>> pkts;
};

bool capture_tx(const std::uint8_t* data, std::size_t len, void* ctx) {
  auto* c = static_cast<TxCap*>(ctx);
  c->pkts.emplace_back(data, data + len);
  return true;
}

std::vector<std::uint8_t> reassemble_diag(const TxCap& cap) {
  sdp::frame::Reassembler r;
  r.set_diag_allowed(true);
  for (const auto& p : cap.pkts) {
    (void)r.ingest(p.data(), p.size());
  }
  return std::vector<std::uint8_t>(r.message(), r.message() + r.message_len());
}

void feed_message(ble::Link& link, const std::uint8_t* msg, std::size_t len) {
  std::uint8_t seq = 0u;
  std::vector<std::vector<std::uint8_t>> pkts;
  auto save = [](const std::uint8_t* p, std::size_t n, void* ctx) -> bool {
    auto* v = static_cast<std::vector<std::vector<std::uint8_t>>*>(ctx);
    v->emplace_back(p, p + n);
    return true;
  };
  sdp::frame::fragment_message(sdp::frame::kChanDiag, 0, &seq, msg, len, save,
                               &pkts);
  for (const auto& p : pkts) {
    link.on_rx_write(p.data(), p.size());
  }
}

sdp::diag::RenderReport fake_render(const std::uint8_t*, std::size_t len,
                                    void*) {
  sdp::diag::RenderReport r;
  r.parse_us = 100u;
  r.render_us = 200u;
  r.ops = 5u;
  r.status = 0u;
  (void)len;
  return r;
}

sdp::diag::MbufReport fake_mbuf(void*) {
  sdp::diag::MbufReport r;
  r.peak_used = 3u;
  r.block_count = 8u;
  r.block_size = 292u;
  r.free_now = 5u;
  return r;
}

void test_rtt_echo() {
  ble::Link link;
  link.init(true);
  TxCap cap;
  link.set_tx(capture_tx, &cap);

  sdp::diag::Bench bench;
  bench.init(fake_now, [](const std::uint8_t* m, std::size_t n, void* c) {
    return static_cast<ble::Link*>(c)->reply_diag(m, n);
  }, &link, fake_render, nullptr, fake_mbuf, nullptr);
  link.set_diag_bench(&bench);

  std::uint8_t req[9] = {sdp::diag::kOpRttReq, 1, 2, 3, 4, 5, 6, 7, 8};
  g_fake_us = 4242u;
  feed_message(link, req, sizeof(req));

  expect_eq("rtt loopbacks", link.loopback_count(), 1u);
  auto msg = reassemble_diag(cap);
  expect_true("rtt len", msg.size() == 1u + 8u + 4u);
  expect_eq("rtt op", msg[0], sdp::diag::kOpRttRsp);
  expect_true("rtt token", std::memcmp(msg.data() + 1, req + 1, 8) == 0);
  const std::uint32_t stamp = static_cast<std::uint32_t>(msg[9]) |
                              (static_cast<std::uint32_t>(msg[10]) << 8) |
                              (static_cast<std::uint32_t>(msg[11]) << 16) |
                              (static_cast<std::uint32_t>(msg[12]) << 24);
  expect_eq("rtt stamp", stamp, 4242u);
}

void test_throughput() {
  ble::Link link;
  link.init(true);
  TxCap cap;
  link.set_tx(capture_tx, &cap);

  sdp::diag::Bench bench;
  bench.init(fake_now, [](const std::uint8_t* m, std::size_t n, void* c) {
    return static_cast<ble::Link*>(c)->reply_diag(m, n);
  }, &link, fake_render, nullptr, fake_mbuf, nullptr);
  link.set_diag_bench(&bench);

  g_fake_us = 1000u;
  std::uint8_t start[5] = {sdp::diag::kOpThruStart, 10, 0, 0, 0};  // expect 10
  feed_message(link, start, sizeof(start));
  expect_true("thru active", bench.thru_active());

  std::uint8_t data[1 + 10];
  data[0] = sdp::diag::kOpThruData;
  for (int i = 0; i < 10; ++i) data[1 + i] = static_cast<std::uint8_t>(i);
  g_fake_us = 1000u + 5000u;  // 5 ms later → 10 B / 5ms = 2 kB/s → kbps_x100=200
  feed_message(link, data, sizeof(data));

  expect_true("thru done", !bench.thru_active());
  auto msg = reassemble_diag(cap);
  expect_eq("thru op", msg[0], sdp::diag::kOpThruResult);
  expect_eq("thru bytes",
            static_cast<unsigned>(msg[5]) | (static_cast<unsigned>(msg[6]) << 8),
            10u);
  const std::uint32_t kbps_x100 =
      static_cast<std::uint32_t>(msg[9]) |
      (static_cast<std::uint32_t>(msg[10]) << 8) |
      (static_cast<std::uint32_t>(msg[11]) << 16) |
      (static_cast<std::uint32_t>(msg[12]) << 24);
  expect_eq("thru kbps_x100", kbps_x100, 200u);
}

void test_render_and_mbuf() {
  ble::Link link;
  link.init(true);
  TxCap cap;
  link.set_tx(capture_tx, &cap);

  sdp::diag::Bench bench;
  bench.init(fake_now, [](const std::uint8_t* m, std::size_t n, void* c) {
    return static_cast<ble::Link*>(c)->reply_diag(m, n);
  }, &link, fake_render, nullptr, fake_mbuf, nullptr);
  link.set_diag_bench(&bench);

  std::uint8_t render_req[] = {sdp::diag::kOpRenderReq, 0x10, 0x00};
  feed_message(link, render_req, sizeof(render_req));
  auto msg = reassemble_diag(cap);
  expect_eq("render op", msg[0], sdp::diag::kOpRenderRsp);
  expect_eq("parse_us", msg[1], 100u);
  expect_eq("render_us", msg[5], 200u);

  cap.pkts.clear();
  std::uint8_t mbuf_req[] = {sdp::diag::kOpMbufReq};
  feed_message(link, mbuf_req, sizeof(mbuf_req));
  msg = reassemble_diag(cap);
  expect_eq("mbuf op", msg[0], sdp::diag::kOpMbufRsp);
  expect_eq("mbuf peak", msg[1], 3u);
  expect_eq("mbuf count", msg[3], 8u);
}

void test_mbuf_tracker() {
  ble::MbufStatsTracker t;
  t.reset();
  t.note_free(8);
  t.note_free(5);
  t.note_free(6);
  auto r = t.report();
  expect_eq("peak used", r.peak_used, 3u);  // 8-5
  expect_eq("free now", r.free_now, 6u);
}

}  // namespace

int main() {
  test_rtt_echo();
  test_throughput();
  test_render_and_mbuf();
  test_mbuf_tracker();
  if (g_failures != 0) {
    std::printf("%d FAILURES\n", g_failures);
    return 1;
  }
  std::printf("all diag bench tests passed\n");
  return 0;
}
