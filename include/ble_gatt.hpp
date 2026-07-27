#pragma once

#include "ble_conn.hpp"
#include "ble_link.hpp"

#include <cstddef>
#include <cstdint>

// GATT surface (§4.1). Transport only — no display/input channels.

namespace ble {

struct GattCaps {
  bool battery = true;
  bool heart_rate = true;
  bool device_info = true;
  bool current_time_client = true;  // CTS as GATT client
};

// STATUS characteristic payload (little-endian), 12 bytes:
//   u16 att_mtu
//   u16 dl_tx_octets
//   u16 dl_rx_octets
//   u8  phy_tx
//   u8  phy_rx
//   u16 conn_interval_units
//   u8  flags
//   u8  reserved
constexpr std::size_t kStatusPayloadLen = 12u;

void encode_status(const StatusSnapshot& s, std::uint8_t out[kStatusPayloadLen]);

class GattServer {
public:
  void init(Link* link, GattCaps caps);

  Link* link() { return link_; }

  // Called from NimBLE (or host harness) when RX is written.
  void on_rx(const std::uint8_t* data, std::size_t len);

  // Fill STATUS value buffer for Read / Notify.
  void refresh_status(const NegotiatedParams& n, bool linked);

  const std::uint8_t* status_value() const { return status_raw_; }
  std::size_t status_len() const { return kStatusPayloadLen; }

  // TX notify hook — platform sets this to ble_gatts_notify / host collector.
  void set_tx_sink(TxNotifyFn fn, void* ctx);

  const GattCaps& caps() const { return caps_; }

private:
  Link* link_ = nullptr;
  GattCaps caps_{};
  std::uint8_t status_raw_[kStatusPayloadLen] = {};
};

// Start FreeRTOS link task + NimBLE host (no-op stub if SLATE_HAS_NIMBLE=0).
void start_stack(GattServer* gatt, SessionProfile profile);

}  // namespace ble
