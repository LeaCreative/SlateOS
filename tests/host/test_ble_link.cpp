#include "ble_conn.hpp"
#include "ble_gatt.hpp"
#include "ble_link.hpp"
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

struct TxCap {
  std::vector<std::vector<std::uint8_t>> pkts;
};

bool capture_tx(const std::uint8_t* data, std::size_t len, void* ctx) {
  auto* c = static_cast<TxCap*>(ctx);
  c->pkts.emplace_back(data, data + len);
  return true;
}

void test_diag_loopback() {
  ble::Link link;
  link.init(true);
  TxCap cap;
  link.set_tx(capture_tx, &cap);

  const std::uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
  std::uint8_t seq = 0u;
  std::vector<std::vector<std::uint8_t>> rx_pkts;
  auto save = [](const std::uint8_t* p, std::size_t n, void* ctx) -> bool {
    auto* v = static_cast<std::vector<std::vector<std::uint8_t>>*>(ctx);
    v->emplace_back(p, p + n);
    return true;
  };
  sdp::frame::fragment_message(sdp::frame::kChanDiag, 0, &seq, payload,
                               sizeof(payload), save, &rx_pkts);

  for (const auto& p : rx_pkts) {
    link.on_rx_write(p.data(), p.size());
  }

  expect_eq("loopback count", link.loopback_count(), 1u);
  expect_true("tx emitted", !cap.pkts.empty());

  // Reassemble TX side and compare.
  sdp::frame::Reassembler r;
  r.set_diag_allowed(true);
  sdp::frame::FrameStatus st = sdp::frame::FrameStatus::Dropped;
  for (const auto& p : cap.pkts) {
    st = r.ingest(p.data(), p.size());
  }
  expect_eq("tx Ok", static_cast<unsigned>(st),
            static_cast<unsigned>(sdp::frame::FrameStatus::Ok));
  expect_eq("tx len", static_cast<unsigned>(r.message_len()), 4u);
  expect_true("echo match", std::memcmp(r.message(), payload, 4) == 0);
}

void test_diag_disabled_no_loopback() {
  ble::Link link;
  link.init(false);
  TxCap cap;
  link.set_tx(capture_tx, &cap);

  const std::uint8_t payload[] = {0x01};
  std::uint8_t seq = 0u;
  std::uint8_t pkt[16];
  const std::size_t n = sdp::frame::encode_fragment(
      sdp::frame::kChanDiag,
      sdp::frame::kFlagFirst | sdp::frame::kFlagLast, seq, 1, payload, 1, pkt,
      sizeof(pkt));
  link.on_rx_write(pkt, n);
  expect_eq("no loopback", link.loopback_count(), 0u);
  expect_eq("drop/reject", link.drop_count(), 1u);
  expect_true("no tx", cap.pkts.empty());
}

std::uint16_t fake_mtu_ok(std::uint16_t) { return 247u; }
std::uint16_t fake_mtu_refuse(std::uint16_t) { return 23u; }
bool fake_dle_ok(std::uint16_t, std::uint16_t) { return true; }
void fake_read_dle(std::uint16_t* tx, std::uint16_t* rx) {
  *tx = 251u;
  *rx = 251u;
}
bool fake_phy(void) { return true; }
void fake_read_phy(std::uint8_t* tx, std::uint8_t* rx) {
  *tx = 2u;
  *rx = 2u;
}
bool fake_conn(std::uint16_t, std::uint16_t, std::uint16_t) { return true; }
void fake_read_conn(std::uint16_t* iv, std::uint16_t* lat, std::uint16_t* to) {
  *iv = 24u;
  *lat = 0u;
  *to = 400u;
}

void test_negotiate_success() {
  ble::ConnHooks h;
  h.request_mtu = fake_mtu_ok;
  h.request_dle = fake_dle_ok;
  h.read_dle = fake_read_dle;
  h.request_phy_2m = fake_phy;
  h.read_phy = fake_read_phy;
  h.request_conn_params = fake_conn;
  h.read_conn_params = fake_read_conn;

  ble::NegotiatedParams n;
  ble::negotiate(ble::SessionProfile::Active, h, &n);
  expect_true("mtu_ok", n.mtu_ok);
  expect_true("dle_ok", n.dle_ok);
  expect_true("phy_ok", n.phy_ok);
  expect_eq("mtu", n.att_mtu, 247u);
}

void test_negotiate_mtu_refuse() {
  ble::ConnHooks h;
  h.request_mtu = fake_mtu_refuse;
  h.request_dle = fake_dle_ok;
  h.read_dle = fake_read_dle;
  h.request_phy_2m = fake_phy;
  h.read_phy = fake_read_phy;
  h.request_conn_params = fake_conn;
  h.read_conn_params = fake_read_conn;

  ble::NegotiatedParams n;
  ble::negotiate(ble::SessionProfile::Active, h, &n);
  expect_true("mtu not ok", !n.mtu_ok);
  expect_eq("mtu 23", n.att_mtu, 23u);
}

void test_status_encode() {
  ble::StatusSnapshot s;
  s.att_mtu = 247;
  s.dl_tx_octets = 251;
  s.dl_rx_octets = 251;
  s.phy_tx = 2;
  s.phy_rx = 2;
  s.conn_interval_units = 24;
  s.flags = 0x03;
  std::uint8_t raw[ble::kStatusPayloadLen];
  ble::encode_status(s, raw);
  expect_eq("mtu lo", raw[0], 247u & 0xFF);
  expect_eq("mtu hi", raw[1], 0u);
  expect_eq("phy", raw[6], 2u);
  expect_eq("flags", raw[10], 0x03u);
}

}  // namespace

int main() {
  test_diag_loopback();
  test_diag_disabled_no_loopback();
  test_negotiate_success();
  test_negotiate_mtu_refuse();
  test_status_encode();
  if (g_failures != 0) {
    std::printf("%d FAILURES\n", g_failures);
    return 1;
  }
  std::printf("all ble link tests passed\n");
  return 0;
}
