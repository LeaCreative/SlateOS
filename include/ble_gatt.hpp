#pragma once

#include "ble_conn.hpp"
#include "ble_link.hpp"

#include <cstddef>
#include <cstdint>

// GATT surface (§4.1). Transport only — no display/input channels.

namespace ble {

struct GattCaps {
  bool battery = true;                 // BAS 0x180F
  bool heart_rate = false;             // HRS deferred (needs HRS3300 driver)
  bool device_info = true;             // DIS 0x180A
  bool current_time_client = true;     // CONTROL 0x20 time sync (not GATT CTS yet)
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

// Invoked when a central is connected and Slate GATT negotiation finished.
// Used for MCUBoot IMAGE_OK confirm — never call from bare main() alone.
using SessionUpFn = void (*)(void* ctx);
void set_session_up_hook(SessionUpFn fn, void* ctx);
void notify_session_up();  // platform / tests call this

using SessionDownFn = void (*)(void* ctx);
void set_session_down_hook(SessionDownFn fn, void* ctx);
void notify_session_down();

// Start FreeRTOS link task + NimBLE host (no-op stub if SLATE_HAS_NIMBLE=0).
// Does not start the scheduler — caller must vTaskStartScheduler().
void start_stack(GattServer* gatt, SessionProfile profile);

/** Push battery % into BAS when linked (no-op if BAS off / NimBLE off). */
void update_battery_level(std::uint8_t percent);

/** True while any central holds a connection — not just a full SDP session.
    A sustained connection is what qualifies an image for IMAGE_OK, because it
    proves the radio, host and GATT server that a future DFU depends on. */
bool central_connected();

bool request_conn_interval(std::uint16_t interval_units);

/**
 * Actively raise ATT MTU / DLE / 2M PHY / connection interval.
 *
 * Not run on connect (N-15): a mirrored peripheral lets the central drive,
 * and issuing all four inside the GAP connect callback collapsed the link.
 * Call this only from the app task, and only once the central has settled
 * (the throughput/RTT gates need it; ordinary sessions do not).
 */
void negotiate_now();

/**
 * Sealed-watch BLE bring-up telemetry (no SWD on sealed units — the diag
 * overlay is the only window). state: 0 stack not started, 1 start_stack
 * entered, 2 port+LL init done, 3 eventqs verified, 4 FreeRTOS host/LL tasks
 * created, 5 host task running, 6 controller synced (on_sync), 7 advertising.
 * Failures: 90 gatts_count_cfg, 91 gatts_add_svcs, 92 adv_set_fields,
 * 93 adv_start, 94 host reset, 95 identity address config, 96 Slate service
 * absent from the built GATT table. rc: the failing call's return code (or
 * reset reason for 94); 0 when healthy. Failure codes are sticky — a later
 * progress mark cannot overwrite one.
 */
void bringup_snapshot(std::uint8_t* state, std::uint16_t* rc);

}  // namespace ble
