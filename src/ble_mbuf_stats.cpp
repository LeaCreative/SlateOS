#include "ble_mbuf_stats.hpp"

namespace ble {

MbufStatsTracker& mbuf_stats() {
  static MbufStatsTracker g;
  return g;
}

}  // namespace ble
