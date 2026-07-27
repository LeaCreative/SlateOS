#pragma once

#include "sdp_diag.hpp"
#include "sdp_frame.hpp"

#include <cstddef>
#include <cstdint>

// Transport link: GATT RX → frame reassembly → channel demux.

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

// Called for completed messages on CONTROL / DISPLAY / INPUT / … (not DIAG).
using AppMessageFn = void (*)(std::uint8_t channel, const std::uint8_t* msg,
                              std::size_t len, void* ctx);

class Link {
public:
  void init(bool diag_allowed);
  void set_tx(TxNotifyFn fn, void* ctx);
  void set_app_handler(AppMessageFn fn, void* ctx);
  void set_status(const StatusSnapshot& s);
  StatusSnapshot status() const { return status_; }

  void set_diag_bench(sdp::diag::Bench* bench) { diag_bench_ = bench; }
  sdp::diag::Bench* diag_bench() const { return diag_bench_; }

  void on_rx_write(const std::uint8_t* data, std::size_t len);

  bool send_message(std::uint8_t channel, const std::uint8_t* msg, std::size_t len);

  bool reply_diag(const std::uint8_t* msg, std::size_t len);

  std::uint32_t drop_count() const { return drops_; }
  std::uint32_t loopback_count() const { return loopbacks_; }

private:
  static bool tx_emit(const std::uint8_t* pkt, std::size_t len, void* ctx);

  sdp::frame::Reassembler reasm_{};
  TxNotifyFn tx_ = nullptr;
  void* tx_ctx_ = nullptr;
  AppMessageFn app_ = nullptr;
  void* app_ctx_ = nullptr;
  std::uint8_t tx_seq_[sdp::frame::kChannelCount] = {};
  StatusSnapshot status_{};
  bool diag_allowed_ = false;
  std::uint32_t drops_ = 0u;
  std::uint32_t loopbacks_ = 0u;
  sdp::diag::Bench* diag_bench_ = nullptr;
};

}  // namespace ble
