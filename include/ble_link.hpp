#pragma once

#include "ble_app_inbox.hpp"
#include "sdp_diag.hpp"
#include "sdp_frame.hpp"

#include <cstddef>
#include <cstdint>

// Transport link: GATT RX → frame reassembly → AppInbox (link→app handoff).
// DIAG may still complete on the host task (loopback / bench); all other
// channels are deferred to the app task via drain_app_messages().

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

// Called for completed messages on CONTROL / DISPLAY / … from the *app* task
// after drain_app_messages() (never from the NimBLE host task).
using AppMessageFn = void (*)(std::uint8_t channel, const std::uint8_t* msg,
                              std::size_t len, void* ctx);

/** Optional wake after enqueue (e.g. xTaskNotify). May run on host task. */
using AppWakeFn = void (*)(void* ctx);

class Link {
public:
  void init(bool diag_allowed);
  void set_tx(TxNotifyFn fn, void* ctx);
  void set_app_handler(AppMessageFn fn, void* ctx);
  void set_app_wake(AppWakeFn fn, void* ctx);
  void set_status(const StatusSnapshot& s);
  StatusSnapshot status() const { return status_; }

  void set_diag_bench(sdp::diag::Bench* bench) { diag_bench_ = bench; }
  sdp::diag::Bench* diag_bench() const { return diag_bench_; }

  /** NimBLE host task / GATT write path — reassemble + enqueue only. */
  void on_rx_write(const std::uint8_t* data, std::size_t len);

  /**
   * App task: deliver at most one pending message to the app handler.
   * Returns true if a message was dispatched (session/parse/render belong here).
   */
  bool drain_app_messages();

  bool send_message(std::uint8_t channel, const std::uint8_t* msg, std::size_t len);

  bool reply_diag(const std::uint8_t* msg, std::size_t len);

  // Includes reassemblies abandoned by the single-in-flight rule (§4.2).
  std::uint32_t drop_count() const { return drops_ + reasm_.preempt_drop_count(); }

  /** Frames refused because their channel is disabled — not a fault. */
  std::uint32_t channel_reject_count() const { return channel_rejects_; }
  std::uint32_t loopback_count() const { return loopbacks_; }
  std::uint32_t handoff_drop_count() const { return inbox_.drop_count(); }
  bool inbox_busy() const { return inbox_.busy(); }

  static constexpr std::size_t inbox_storage_bytes() {
    return AppInbox::storage_bytes();
  }

private:
  static bool tx_emit(const std::uint8_t* pkt, std::size_t len, void* ctx);

  sdp::frame::Reassembler reasm_{};
  AppInbox inbox_{};
  TxNotifyFn tx_ = nullptr;
  void* tx_ctx_ = nullptr;
  AppMessageFn app_ = nullptr;
  void* app_ctx_ = nullptr;
  AppWakeFn wake_ = nullptr;
  void* wake_ctx_ = nullptr;
  std::uint8_t tx_seq_[sdp::frame::kChannelCount] = {};
  StatusSnapshot status_{};
  bool diag_allowed_ = false;
  std::uint32_t drops_ = 0u;
  std::uint32_t channel_rejects_ = 0u;
  std::uint32_t loopbacks_ = 0u;
  sdp::diag::Bench* diag_bench_ = nullptr;
};

}  // namespace ble
