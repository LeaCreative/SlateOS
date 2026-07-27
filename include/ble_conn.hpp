#pragma once

#include <cstdint>

// Negotiated link parameters — §4.1 order:
// ATT MTU 247 → DLE → 2M PHY → connection interval for session state.

namespace ble {

enum class SessionProfile : std::uint8_t {
  Ambient = 0,   // longer interval, lower power
  Active  = 1,   // interactive UI
  Streaming = 2  // patches / camera
};

struct NegotiatedParams {
  std::uint16_t att_mtu = 23u;          // default BLE ATT
  std::uint16_t dl_tx_octets = 27u;     // default LL
  std::uint16_t dl_rx_octets = 27u;
  std::uint8_t  phy_tx = 1u;            // 1 = 1M, 2 = 2M
  std::uint8_t  phy_rx = 1u;
  std::uint16_t conn_interval_units = 24u;  // 1.25 ms units (24 = 30 ms)
  std::uint16_t conn_latency = 0u;
  std::uint16_t supervision_timeout_units = 400u;  // 10 ms units
  bool mtu_ok = false;
  bool dle_ok = false;
  bool phy_ok = false;
  bool interval_ok = false;
};

constexpr std::uint16_t kTargetAttMtu = 247u;
constexpr std::uint16_t kTargetDlOctets = 251u;

// Connection interval targets (1.25 ms units).
inline std::uint16_t interval_for(SessionProfile p) {
  switch (p) {
    case SessionProfile::Ambient:   return 80u;   // 100 ms
    case SessionProfile::Streaming: return 12u;   // 15 ms
    case SessionProfile::Active:
    default:                        return 24u;   // 30 ms
  }
}

// Drive the post-connect negotiation sequence. Platform hooks are injected
// so host tests can simulate phone refusals.
struct ConnHooks {
  // Request ATT MTU; return value actually granted (or 0 on failure).
  std::uint16_t (*request_mtu)(std::uint16_t want) = nullptr;
  // Request DLE; return true if controller accepted (check octets after).
  bool (*request_dle)(std::uint16_t tx_octets, std::uint16_t rx_octets) = nullptr;
  void (*read_dle)(std::uint16_t* tx, std::uint16_t* rx) = nullptr;
  // Request 2M PHY; return true if request submitted.
  bool (*request_phy_2m)() = nullptr;
  void (*read_phy)(std::uint8_t* tx, std::uint8_t* rx) = nullptr;
  // Request connection params; return true if request submitted.
  bool (*request_conn_params)(std::uint16_t interval_units,
                              std::uint16_t latency,
                              std::uint16_t timeout_units) = nullptr;
  void (*read_conn_params)(std::uint16_t* interval_units,
                           std::uint16_t* latency,
                           std::uint16_t* timeout_units) = nullptr;
  void (*log)(const char* msg) = nullptr;
};

// Runs MTU → DLE → PHY → interval. Fills `out` with what was ACTUALLY granted.
void negotiate(SessionProfile profile, const ConnHooks& hooks, NegotiatedParams* out);

}  // namespace ble
