#include "ble_conn.hpp"

namespace ble {
namespace {

void log(const ConnHooks& h, const char* msg) {
  if (h.log) {
    h.log(msg);
  }
}

}  // namespace

void negotiate(SessionProfile profile, const ConnHooks& hooks, NegotiatedParams* out) {
  if (out == nullptr) {
    return;
  }
  *out = NegotiatedParams{};

  // 1) ATT MTU
  log(hooks, "negotiate: request ATT MTU 247");
  if (hooks.request_mtu) {
    out->att_mtu = hooks.request_mtu(kTargetAttMtu);
  }
  out->mtu_ok = (out->att_mtu >= kTargetAttMtu);
  if (!out->mtu_ok) {
    log(hooks, "negotiate: ATT MTU below target (phone may have refused)");
  }

  // 2) Data Length Extension
  log(hooks, "negotiate: request DLE 251/251");
  if (hooks.request_dle) {
    out->dle_ok = hooks.request_dle(kTargetDlOctets, kTargetDlOctets);
  }
  if (hooks.read_dle) {
    hooks.read_dle(&out->dl_tx_octets, &out->dl_rx_octets);
  }
  if (out->dl_tx_octets < 64u || out->dl_rx_octets < 64u) {
    out->dle_ok = false;
    log(hooks, "negotiate: DLE short — throughput capped near 54 kB/s");
  }

  // 3) 2M PHY
  log(hooks, "negotiate: request 2M PHY");
  if (hooks.request_phy_2m) {
    (void)hooks.request_phy_2m();
  }
  if (hooks.read_phy) {
    hooks.read_phy(&out->phy_tx, &out->phy_rx);
  }
  out->phy_ok = (out->phy_tx == 2u && out->phy_rx == 2u);
  if (!out->phy_ok) {
    log(hooks, "negotiate: PHY not 2M (phone or link refused)");
  }

  // 4) Connection interval for session profile
  const std::uint16_t want_iv = interval_for(profile);
  log(hooks, "negotiate: request connection interval");
  if (hooks.request_conn_params) {
    (void)hooks.request_conn_params(want_iv, 0u, 400u);
  }
  if (hooks.read_conn_params) {
    hooks.read_conn_params(&out->conn_interval_units, &out->conn_latency,
                           &out->supervision_timeout_units);
  }
  out->interval_ok = (out->conn_interval_units > 0u);
  log(hooks, "negotiate: complete — log actual MTU/DLE/PHY/interval via STATUS");
}

}  // namespace ble
