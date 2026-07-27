#pragma once

#include "sdp_frame.hpp"

#include <cstddef>
#include <cstdint>

// Transport link: GATT RX → frame reassembly → channel demux.
// Channel 7 (DIAG) loopback in debug builds. No display/input handling yet (M5).

namespace ble {

struct StatusSnapshot {
  std::uint16_t att_mtu = 23u;
  std::uint16_t dl_tx_octets = 27u;
  std::uint16_t dl_rx_octets = 27u;
  std::uint8_t  phy_tx = 1u;
  std::uint8_t  phy_rx = 1u;
  std::uint16_t conn_interval_units = 0u;
  std::uint8_t  flags = 0u;  // bit0 linked, bit1 diag_enabled
};

using TxNotifyFn = bool (*)(const std::uint8_t* data, std::size_t len, void* ctx);

class Link {
public:
  void init(bool diag_allowed);
  void set_tx(TxNotifyFn fn, void* ctx);
  void set_status(const StatusSnapshot& s);
  StatusSnapshot status() const { return status_; }

  // GATT RX characteristic write (Write Without Response).
  void on_rx_write(const std::uint8_t* data, std::size_t len);

  // Encode and notify a complete SDP message on `channel`.
  bool send_message(std::uint8_t channel, const std::uint8_t* msg, std::size_t len);

  std::uint32_t drop_count() const { return drops_; }
  std::uint32_t loopback_count() const { return loopbacks_; }

private:
  static bool tx_emit(const std::uint8_t* pkt, std::size_t len, void* ctx);

  sdp::frame::Reassembler reasm_{};
  TxNotifyFn tx_ = nullptr;
  void* tx_ctx_ = nullptr;
  std::uint8_t tx_seq_[sdp::frame::kChannelCount] = {};
  StatusSnapshot status_{};
  bool diag_allowed_ = false;
  std::uint32_t drops_ = 0u;
  std::uint32_t loopbacks_ = 0u;
};

}  // namespace ble
