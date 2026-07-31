#pragma once

#include <cstdint>

// Pure policy for when GAP should restart undirected advertising.
// Host-tested; wired from ble_nimble gap_event (InfiniTime: resume on
// connect.status != 0 as well as disconnect).

namespace ble {
namespace gap_adv {

enum class EventKind : std::uint8_t {
  ConnectOk = 0,
  ConnectFailed = 1,
  Disconnect = 2,
  Other = 3,
};

inline bool should_resume(EventKind kind) {
  return kind == EventKind::ConnectFailed || kind == EventKind::Disconnect;
}

// Map NimBLE-ish connect status: 0 = success.
inline EventKind connect_event(int status) {
  return status == 0 ? EventKind::ConnectOk : EventKind::ConnectFailed;
}

}  // namespace gap_adv
}  // namespace ble
