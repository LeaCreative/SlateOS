#include "ble_gatt.hpp"

#include "freertos_smoke.hpp"
#include "rtt.hpp"

#include <cstring>

namespace ble {

namespace {

SessionUpFn g_session_up = nullptr;
void* g_session_up_ctx = nullptr;
SessionDownFn g_session_down = nullptr;
void* g_session_down_ctx = nullptr;

}  // namespace

void set_session_up_hook(SessionUpFn fn, void* ctx) {
  g_session_up = fn;
  g_session_up_ctx = ctx;
}

void notify_session_up() {
  if (g_session_up) {
    g_session_up(g_session_up_ctx);
  }
}

void set_session_down_hook(SessionDownFn fn, void* ctx) {
  g_session_down = fn;
  g_session_down_ctx = ctx;
}

void notify_session_down() {
  if (g_session_down) {
    g_session_down(g_session_down_ctx);
  }
}

void encode_status(const StatusSnapshot& s, std::uint8_t out[kStatusPayloadLen]) {
  out[0] = static_cast<std::uint8_t>(s.att_mtu & 0xFFu);
  out[1] = static_cast<std::uint8_t>((s.att_mtu >> 8) & 0xFFu);
  out[2] = static_cast<std::uint8_t>(s.dl_tx_octets & 0xFFu);
  out[3] = static_cast<std::uint8_t>((s.dl_tx_octets >> 8) & 0xFFu);
  out[4] = static_cast<std::uint8_t>(s.dl_rx_octets & 0xFFu);
  out[5] = static_cast<std::uint8_t>((s.dl_rx_octets >> 8) & 0xFFu);
  out[6] = s.phy_tx;
  out[7] = s.phy_rx;
  out[8] = static_cast<std::uint8_t>(s.conn_interval_units & 0xFFu);
  out[9] = static_cast<std::uint8_t>((s.conn_interval_units >> 8) & 0xFFu);
  out[10] = s.flags;
  out[11] = 0u;
}

void GattServer::init(Link* link, GattCaps caps) {
  link_ = link;
  caps_ = caps;
  std::memset(status_raw_, 0, sizeof(status_raw_));
}

void GattServer::on_rx(const std::uint8_t* data, std::size_t len) {
  if (link_ != nullptr) {
    link_->on_rx_write(data, len);
  }
}

void GattServer::refresh_status(const NegotiatedParams& n, bool linked) {
  StatusSnapshot s;
  s.att_mtu = n.att_mtu;
  s.dl_tx_octets = n.dl_tx_octets;
  s.dl_rx_octets = n.dl_rx_octets;
  s.phy_tx = n.phy_tx;
  s.phy_rx = n.phy_rx;
  s.conn_interval_units = n.conn_interval_units;
  s.flags = linked ? 1u : 0u;
  if (link_ != nullptr) {
    link_->set_status(s);
  }
  encode_status(s, status_raw_);
  // STATUS[11] reserved → M5a FreeRTOS smoke result (0=pending, 1=pass, 2=fail).
  status_raw_[11] = static_cast<std::uint8_t>(freertos_smoke::result());
}

void GattServer::set_tx_sink(TxNotifyFn fn, void* ctx) {
  if (link_ != nullptr) {
    link_->set_tx(fn, ctx);
  }
}

#if !defined(SLATE_HAS_NIMBLE) || (SLATE_HAS_NIMBLE == 0)

void start_stack(GattServer* gatt, SessionProfile profile) {
  (void)profile;
  if (gatt == nullptr) {
    return;
  }
  rtt::log(rtt::Level::Warn,
           "BLE: NimBLE not linked (SLATE_HAS_NIMBLE=0) — transport ready, radio idle");
  rtt::log(rtt::Level::Info, "BLE: Slate UUID base e979acfb-c338-44fa-a962-e96e4cf078f3");
  rtt::log(rtt::Level::Info, "BLE: Service e979acfb-c338-0000-a962-e96e4cf078f3");
}

bool request_conn_interval(std::uint16_t interval_units) {
  (void)interval_units;
  return false;
}

void update_battery_level(std::uint8_t percent) {
  (void)percent;
}

void set_hrs_cccd_hook(HrsCccdFn fn, void* ctx) {
  (void)fn;
  (void)ctx;
}

void update_heart_rate(std::uint8_t bpm) {
  (void)bpm;
}

bool central_connected() { return false; }

void bringup_snapshot(std::uint8_t* state, std::uint16_t* rc) {
  if (state != nullptr) *state = 0u;
  if (rc != nullptr) *rc = 0u;
}

bool dfu_busy() { return false; }

#endif

}  // namespace ble
