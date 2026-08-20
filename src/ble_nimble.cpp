// NimBLE + FreeRTOS glue for Slate GATT (§4.1) and post-connect negotiation.
// Compiled only when SLATE_HAS_NIMBLE=1 and the Apache NimBLE tree is on the
// include/link path (see docs/ble.md).

#include "ble_gatt.hpp"
#include "ble_gap_adv_policy.hpp"
#include "ble_mbuf_stats.hpp"
#include "boot_diag.hpp"
#include "dfu_nordic.hpp"
#include "nrf52832_regs.hpp"
#include "ota_slot.hpp"
#include "battery.hpp"
#include "slate_uuids.hpp"
#include "rtt.hpp"
#include "sdp_frame.hpp"

#if defined(SLATE_HAS_NIMBLE) && (SLATE_HAS_NIMBLE == 1)

extern "C" {
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/bas/ble_svc_bas.h"
#include "services/dis/ble_svc_dis.h"
#if defined(NIMBLE_CFG_CONTROLLER) && (NIMBLE_CFG_CONTROLLER == 1)
#include "controller/ble_ll.h"
// A sysinit entry in nimble/controller/pkg.yml, so it has no public prototype.
void ble_ll_init(void);
#endif
}

#include <cstring>

namespace ble {
namespace {

// Bring-up telemetry for the sealed diag overlay — see bringup_snapshot() in
// ble_gatt.hpp for the state map. Written from main, host task and sync
// callback; single-writer per phase, u8/u16 stores are atomic on Cortex-M4.
volatile std::uint8_t g_bringup_state = 0u;
volatile std::uint16_t g_bringup_rc = 0u;

void bringup_mark(std::uint8_t state, int rc = 0) {
  // Failures (>= 90) are sticky: a later progress mark must not erase them.
  // start_stack records a registration failure and keeps going, so without
  // this the subsequent "advertising" mark hid it.
  if (g_bringup_state >= 90u && state < 90u) {
    return;
  }
  g_bringup_state = state;
  g_bringup_rc = static_cast<std::uint16_t>(rc);
}

GattServer* g_gatt = nullptr;
SessionProfile g_profile = SessionProfile::Active;
// Own-address type for advertising, inferred in on_sync after the identity is
// configured (BLE_OWN_ADDR_RANDOM on nRF52 — there is no public address).
std::uint8_t g_own_addr_type = BLE_OWN_ADDR_PUBLIC;
std::uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
std::uint16_t g_attr_tx = 0;
std::uint16_t g_attr_status = 0;
std::uint16_t g_attr_dfu_ctrl = 0;
std::uint16_t g_attr_hrm = 0;
bool g_bas_inited = false;
bool g_hrs_notify = false;
std::uint8_t g_hrs_bpm = 0u;
HrsCccdFn g_hrs_cccd = nullptr;
void* g_hrs_cccd_ctx = nullptr;
slate::dfu::Engine g_dfu{};
std::uint8_t g_phy_tx = 1u;
std::uint8_t g_phy_rx = 1u;
NegotiatedParams g_last_nego{};

bool dfu_erase(void*) { return slate::ota_slot::erase_all(); }
bool dfu_write(std::uint32_t off, const std::uint8_t* d, std::size_t n, void*) {
  return slate::ota_slot::write(off, d, n);
}
bool dfu_magic(void*) { return slate::ota_slot::write_infinitime_pending_magic(); }
std::uint8_t dfu_batt(void*) { return slate::battery::percent(); }
bool dfu_chg(void*) { return slate::battery::charging(); }
void dfu_reboot(void*) { slate::ota_slot::request_reboot(); }
void dfu_notify(const std::uint8_t* rsp, std::size_t len, void*) {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || g_attr_dfu_ctrl == 0 ||
      rsp == nullptr) {
    return;
  }
  struct os_mbuf* om =
      ble_hs_mbuf_from_flat(rsp, static_cast<std::uint16_t>(len));
  if (om) {
    (void)ble_gatts_notify_custom(g_conn_handle, g_attr_dfu_ctrl, om);
  }
}

int gap_event(struct ble_gap_event* event, void* arg);

void resume_advertising() {
  // Adv fields were set in on_sync; restart undirected advertising only.
  struct ble_gap_adv_params adv = {};
  adv.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
  (void)ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &adv,
                          gap_event, nullptr);
}

bool tx_notify(const std::uint8_t* data, std::size_t len, void* ctx) {
  (void)ctx;
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || g_attr_tx == 0) {
    return false;
  }
  struct os_mbuf* om = ble_hs_mbuf_from_flat(data, static_cast<std::uint16_t>(len));
  mbuf_stats().note_free(static_cast<std::uint16_t>(os_msys_num_free()));
  if (om == nullptr) {
    return false;
  }
  const int rc = ble_gatts_notify_custom(g_conn_handle, g_attr_tx, om);
  return rc == 0;
}

void log_line(const char* msg) {
  rtt::log(rtt::Level::Info, msg);
}

/** Push the current STATUS value if a central is connected and subscribed. */
void publish_status_notify() {
  if (g_gatt == nullptr || g_attr_status == 0 ||
      g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  struct os_mbuf* om =
      ble_hs_mbuf_from_flat(g_gatt->status_value(), g_gatt->status_len());
  if (om) {
    (void)ble_gatts_notify_custom(g_conn_handle, g_attr_status, om);
  }
}

/** Snapshot live GAP params into g_last_nego and refresh the STATUS buffer. */
void capture_conn_into_status() {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  struct ble_gap_conn_desc desc;
  if (ble_gap_conn_find(g_conn_handle, &desc) == 0) {
    g_last_nego.conn_interval_units = desc.conn_itvl;
  }
  if (g_gatt != nullptr) {
    g_gatt->refresh_status(g_last_nego, true);
  }
}

std::uint16_t hook_mtu(std::uint16_t want) {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return 23u;
  }
  (void)ble_att_set_preferred_mtu(want);
  (void)ble_gattc_exchange_mtu(g_conn_handle, nullptr, nullptr);
  // Preferred; actual value arrives asynchronously — read current.
  return ble_att_mtu(g_conn_handle);
}

bool hook_dle(std::uint16_t tx, std::uint16_t rx) {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }
  return ble_gap_set_data_len(g_conn_handle, tx, rx) == 0;
}

void hook_read_dle(std::uint16_t* tx, std::uint16_t* rx) {
  // NimBLE does not always expose a sync getter; leave last-requested as hint.
  if (tx) *tx = kTargetDlOctets;
  if (rx) *rx = kTargetDlOctets;
}

bool hook_phy() {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }
  const int rc = ble_gap_set_prefered_le_phy(
      g_conn_handle, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK,
      BLE_GAP_LE_PHY_CODED_ANY);
  return rc == 0;
}

void hook_read_phy(std::uint8_t* tx, std::uint8_t* rx) {
  if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    uint8_t t = 0;
    uint8_t r = 0;
    if (ble_gap_read_le_phy(g_conn_handle, &t, &r) == 0) {
      g_phy_tx = t;
      g_phy_rx = r;
    }
  }
  if (tx) *tx = g_phy_tx;
  if (rx) *rx = g_phy_rx;
}

bool hook_conn_params(std::uint16_t iv, std::uint16_t lat, std::uint16_t to) {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }
  struct ble_gap_upd_params p = {};
  p.itvl_min = iv;
  p.itvl_max = iv;
  p.latency = lat;
  p.supervision_timeout = to;
  p.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
  p.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;
  return ble_gap_update_params(g_conn_handle, &p) == 0;
}

void hook_read_conn(std::uint16_t* iv, std::uint16_t* lat, std::uint16_t* to) {
  if (iv) *iv = interval_for(g_profile);
  if (lat) *lat = 0u;
  if (to) *to = 400u;
}

// Peripheral-driven MTU/DLE/PHY/param negotiation. NOT run on connect any
// more (N-15) — it is kept because the throughput/RTT/render gates (roadmap
// A/B/D) need those parameters actively raised. Call it deliberately, from
// the app task, once the central has finished discovery — never from inside
// a GAP callback.
void run_negotiate() {
  ConnHooks hooks;
  hooks.request_mtu = hook_mtu;
  hooks.request_dle = hook_dle;
  hooks.read_dle = hook_read_dle;
  hooks.request_phy_2m = hook_phy;
  hooks.read_phy = hook_read_phy;
  hooks.request_conn_params = hook_conn_params;
  hooks.read_conn_params = hook_read_conn;
  hooks.log = log_line;

  NegotiatedParams n;
  negotiate(g_profile, hooks, &n);
  g_last_nego = n;
  g_phy_tx = n.phy_tx ? n.phy_tx : g_phy_tx;
  g_phy_rx = n.phy_rx ? n.phy_rx : g_phy_rx;
  if (g_gatt != nullptr) {
    g_gatt->refresh_status(n, true);
    publish_status_notify();
  }

  rtt::write("BLE negotiated MTU=0x");
  rtt::write_hex(n.att_mtu);
  rtt::write(" DLE_tx=0x");
  rtt::write_hex(n.dl_tx_octets);
  rtt::write(" PHY_tx=0x");
  rtt::write_hex(n.phy_tx);
  rtt::write(" iv=0x");
  rtt::write_hex(n.conn_interval_units);
  rtt::write_line("");
  // Session-up is signalled from the connect event now (N-15), not here.
}

static int rx_access(std::uint16_t conn, std::uint16_t attr,
                     struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  std::uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  std::uint8_t buf[sdp::frame::kAttPayloadMax];
  if (len > sizeof(buf)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, nullptr);
  if (rc != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  mbuf_stats().note_free(static_cast<std::uint16_t>(os_msys_num_free()));
  if (g_gatt) {
    g_gatt->on_rx(buf, len);
  }
  return 0;
}

static int status_access(std::uint16_t conn, std::uint16_t attr,
                         struct ble_gatt_access_ctxt* ctxt, void* arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (g_gatt == nullptr) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  int rc = os_mbuf_append(ctxt->om, g_gatt->status_value(), g_gatt->status_len());
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int tx_access(std::uint16_t, std::uint16_t, struct ble_gatt_access_ctxt*, void*) {
  return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int dfu_ctrl_access(std::uint16_t, std::uint16_t,
                           struct ble_gatt_access_ctxt* ctxt, void*) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  std::uint8_t buf[32];
  std::uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len > sizeof(buf)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  if (ble_hs_mbuf_to_flat(ctxt->om, buf, len, nullptr) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  g_dfu.on_control(buf, len);
  return 0;
}

static int dfu_pkt_access(std::uint16_t, std::uint16_t,
                          struct ble_gatt_access_ctxt* ctxt, void*) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  std::uint8_t buf[sdp::frame::kAttPayloadMax];
  std::uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len > sizeof(buf)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  if (ble_hs_mbuf_to_flat(ctxt->om, buf, len, nullptr) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  g_dfu.on_packet(buf, len);
  return 0;
}

// N-17: these bytes are the little-endian (fully reversed) form of the text
// UUIDs. They previously used the Microsoft GUID mixed-endian layout, so the
// watch published f378f04c-6ee9-62a9-44fa-0000e979acfb and no central looking
// for the real service could match — while uuid_dfu_* below, written as a
// correct full reversal, worked fine. The static_asserts bind these to
// include/slate_uuids.hpp so the two definitions can never drift again;
// tests/host/test_uuids.cpp pins that header to the canonical strings, which
// the companion (SlateUuids.kt) also hard-codes.
static const ble_uuid128_t uuid_svc = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x00, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);
static const ble_uuid128_t uuid_rx = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x01, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);
static const ble_uuid128_t uuid_tx = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x02, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);
static const ble_uuid128_t uuid_status = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x03, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);

namespace {
constexpr ble_uuid128_t kUuidSvcCheck = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x00, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);
constexpr ble_uuid128_t kUuidRxCheck = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x01, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);
constexpr ble_uuid128_t kUuidTxCheck = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x02, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);
constexpr ble_uuid128_t kUuidStatusCheck = BLE_UUID128_INIT(
    0xf3, 0x78, 0xf0, 0x4c, 0x6e, 0xe9, 0x62, 0xa9, 0x03, 0x00, 0x38, 0xc3,
    0xfb, 0xac, 0x79, 0xe9);

constexpr bool uuid_matches(const std::uint8_t (&a)[16],
                            const std::uint8_t (&b)[16]) {
  for (int i = 0; i < 16; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}
static_assert(uuid_matches(kUuidSvcCheck.value, slate::uuid::kService),
              "service UUID differs from include/slate_uuids.hpp");
static_assert(uuid_matches(kUuidRxCheck.value, slate::uuid::kRx),
              "RX UUID differs from include/slate_uuids.hpp");
static_assert(uuid_matches(kUuidTxCheck.value, slate::uuid::kTx),
              "TX UUID differs from include/slate_uuids.hpp");
static_assert(uuid_matches(kUuidStatusCheck.value, slate::uuid::kStatus),
              "STATUS UUID differs from include/slate_uuids.hpp");
}  // namespace
static const ble_uuid128_t uuid_dfu_svc = BLE_UUID128_INIT(
    0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef, 0x12, 0x12,
    0x30, 0x15, 0x00, 0x00);
static const ble_uuid128_t uuid_dfu_ctrl = BLE_UUID128_INIT(
    0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef, 0x12, 0x12,
    0x31, 0x15, 0x00, 0x00);
static const ble_uuid128_t uuid_dfu_pkt = BLE_UUID128_INIT(
    0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef, 0x12, 0x12,
    0x32, 0x15, 0x00, 0x00);
// DFU Revision: optional read-only, returns 0x0008 matching InfiniTime.
static const ble_uuid128_t uuid_dfu_rev = BLE_UUID128_INIT(
    0x23, 0xd1, 0xbc, 0xea, 0x5f, 0x78, 0x23, 0x15, 0xde, 0xef, 0x12, 0x12,
    0x34, 0x15, 0x00, 0x00);

static const ble_uuid16_t uuid_hrs_svc = BLE_UUID16_INIT(0x180Du);
static const ble_uuid16_t uuid_hrs_meas = BLE_UUID16_INIT(0x2A37u);

static int hrs_access(std::uint16_t, std::uint16_t,
                      struct ble_gatt_access_ctxt* ctxt, void*) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  std::uint8_t buf[2] = {0u, g_hrs_bpm};
  return os_mbuf_append(ctxt->om, buf, sizeof(buf)) == 0
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_chr_def g_hrs_chrs[] = {
    {
        .uuid = &uuid_hrs_meas.u,
        .access_cb = hrs_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_attr_hrm,
    },
    {0},
};

static const struct ble_gatt_chr_def g_slate_chrs[] = {
    {
        .uuid = &uuid_rx.u,
        .access_cb = rx_access,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &uuid_tx.u,
        .access_cb = tx_access,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_attr_tx,
    },
    {
        .uuid = &uuid_status.u,
        .access_cb = status_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_attr_status,
    },
    {0},
};

static int dfu_rev_access(std::uint16_t, std::uint16_t,
                          struct ble_gatt_access_ctxt* ctxt, void*) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  // Return 0x0008 LE — matching InfiniTime DFU revision.
  static const std::uint8_t kRev[2] = {0x08u, 0x00u};
  return os_mbuf_append(ctxt->om, kRev, sizeof(kRev)) == 0
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_chr_def g_dfu_chrs[] = {
    {
        .uuid = &uuid_dfu_ctrl.u,
        .access_cb = dfu_ctrl_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_attr_dfu_ctrl,
    },
    {
        .uuid = &uuid_dfu_pkt.u,
        .access_cb = dfu_pkt_access,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &uuid_dfu_rev.u,
        .access_cb = dfu_rev_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {0},
};

static const struct ble_gatt_svc_def g_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_svc.u,
        .characteristics = g_slate_chrs,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_dfu_svc.u,
        .characteristics = g_dfu_chrs,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_hrs_svc.u,
        .characteristics = g_hrs_chrs,
    },
    {0},
};

int gap_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        g_conn_handle = event->connect.conn_handle;
        // Mirror InfiniTime (N-15): a peripheral records the connection and
        // then reacts. It does NOT drive MTU exchange, DLE, 2M PHY and a
        // connection-parameter update from inside this callback. Doing all
        // four at once, while the central is still discovering services,
        // is what made the link collapse right after MTU reached 247.
        // The preferred MTU is published once at sync, so the central's own
        // exchange lands at 247 and arrives as BLE_GAP_EVENT_MTU below.
        // Session-up waits for the TX subscription, not this event (N-23).
        // Capture the interval now so a STATUS Read (or later Notify) is not
        // stuck at 0 until a CONN_UPDATE fires — Android often never
        // updates if HIGH priority already matches what it wanted.
        capture_conn_into_status();
        rtt::log(rtt::Level::Info, "BLE: connected — awaiting central");
      } else if (gap_adv::should_resume(
                     gap_adv::connect_event(event->connect.status))) {
        // InfiniTime OnGAPEvent: failed connect must restart advertising or a
        // foreign central's aborted attempt leaves the radio silent.
        rtt::log(rtt::Level::Warn, "BLE: connect failed — resume advertising");
        resume_advertising();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      g_hrs_notify = false;
      if (g_hrs_cccd != nullptr) {
        g_hrs_cccd(false, g_hrs_cccd_ctx);
      }
      rtt::log(rtt::Level::Info, "BLE: disconnected");
      g_dfu.on_disconnect();
      notify_session_down();
      if (gap_adv::should_resume(gap_adv::EventKind::Disconnect)) {
        resume_advertising();
      }
      break;
    case BLE_GAP_EVENT_SUBSCRIBE:
      // The SDP session starts here, when the central subscribes to TX — not
      // on connect (N-23). HELLO_OFFER is a notification, and NimBLE drops
      // notifications to a client that has not written the CCCD. Firing
      // session-up on connect emitted the offer ~1.3 s before the central
      // subscribed, so it was silently discarded: the companion never
      // negotiated, never reached Ready, and therefore never sent CONTROL
      // traffic at all — which is why the watch clock stayed at 1970.
      if (event->subscribe.attr_handle == g_attr_tx) {
        if (event->subscribe.cur_notify) {
          rtt::log(rtt::Level::Info, "BLE: TX subscribed — session up");
          notify_session_up();
        } else {
          rtt::log(rtt::Level::Info, "BLE: TX unsubscribed — session down");
          notify_session_down();
        }
      }
      if (event->subscribe.attr_handle == g_attr_hrm) {
        g_hrs_notify = event->subscribe.cur_notify != 0;
        if (g_hrs_cccd != nullptr) {
          g_hrs_cccd(g_hrs_notify, g_hrs_cccd_ctx);
        }
      }
      // STATUS was only notified from negotiate_now() (benchmark path). Ordinary
      // sessions refreshed the buffer on MTU/PHY/interval events but never
      // pushed it, so the companion's Conn interval stayed blank forever.
      if (event->subscribe.attr_handle == g_attr_status &&
          event->subscribe.cur_notify) {
        capture_conn_into_status();
        publish_status_notify();
      }
      break;
    case BLE_GAP_EVENT_MTU:
      rtt::write("BLE: MTU event mtu=0x");
      rtt::write_hex(event->mtu.value);
      rtt::write_line("");
      // Record whatever the central negotiated so STATUS reports the truth.
      g_last_nego.att_mtu = event->mtu.value;
      g_last_nego.mtu_ok = event->mtu.value >= kTargetAttMtu;
      if (g_gatt != nullptr) {
        g_gatt->refresh_status(g_last_nego, true);
        publish_status_notify();
      }
      break;
    case BLE_GAP_EVENT_CONN_UPDATE:
      // The central owns connection parameters; just record the result.
      if (event->conn_update.status == 0) {
        capture_conn_into_status();
        publish_status_notify();
      }
      break;
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
      if (event->phy_updated.status == 0) {
        g_phy_tx = event->phy_updated.tx_phy;
        g_phy_rx = event->phy_updated.rx_phy;
        g_last_nego.phy_tx = g_phy_tx;
        g_last_nego.phy_rx = g_phy_rx;
        g_last_nego.phy_ok = (g_phy_tx == 2u && g_phy_rx == 2u);
        rtt::write("BLE: PHY updated tx=0x");
        rtt::write_hex(g_phy_tx);
        rtt::write(" rx=0x");
        rtt::write_hex(g_phy_rx);
        rtt::write_line("");
        if (g_gatt != nullptr) {
          g_gatt->refresh_status(g_last_nego, true);
          publish_status_notify();
        }
      }
      break;
    default:
      break;
  }
  return 0;
}

void host_task(void*) {
  bringup_mark(5u);
  nimble_port_run();
}

#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE == 3)
void paint_stage3(std::uint16_t color) { boot_diag::fill(color); }
#else
void paint_stage3(std::uint16_t) {}
#endif

void on_sync() {
  bringup_mark(6u);
#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE == 3)
  paint_stage3(boot_diag::kBlue);
#endif

  // nRF52 has no public IEEE address — FICR.DEVICEADDR is per-chip entropy,
  // and nothing in the standalone port configures a host identity, so
  // ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC) failed with BLE_HS_ENOADDR (21):
  // the N-9 silent radio. Form the BLE static-random identity from FICR (top
  // two bits forced to 11 per spec), the same derivation the mynewt BSP /
  // InfiniTime port uses — stable across boots so CDM associations and bonds
  // stay valid — then advertise with the inferred own-address type.
  {
    std::uint8_t addr[6];
    const std::uint32_t lo = nrf::reg<std::uint32_t>(nrf::ficr::DEVICEADDR_LO);
    const std::uint32_t hi = nrf::reg<std::uint32_t>(nrf::ficr::DEVICEADDR_HI);
    addr[0] = static_cast<std::uint8_t>(lo);
    addr[1] = static_cast<std::uint8_t>(lo >> 8);
    addr[2] = static_cast<std::uint8_t>(lo >> 16);
    addr[3] = static_cast<std::uint8_t>(lo >> 24);
    addr[4] = static_cast<std::uint8_t>(hi);
    addr[5] = static_cast<std::uint8_t>((hi >> 8) | 0xC0u);
    int arc = ble_hs_id_set_rnd(addr);
    if (arc == 0) {
      arc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    }
    if (arc != 0) {
      rtt::log(rtt::Level::Error, "BLE: identity address config failed");
      bringup_mark(95u, arc);
      return;
    }
  }

  // Publish the preferred ATT MTU once, the way a mirrored peripheral does:
  // the central's exchange then lands at 247 without us initiating anything
  // (N-15). SLATE_BLE_ATT_PREFERRED_MTU / MYNEWT_VAL_BLE_ATT_PREFERRED_MTU
  // already size the mbuf pool for it.
  (void)ble_att_set_preferred_mtu(kTargetAttMtu);

  ble_svc_gap_device_name_set("Slate");
  if (g_gatt != nullptr && g_gatt->caps().device_info) {
    ble_svc_dis_manufacturer_name_set("Slate");
    ble_svc_dis_model_number_set("PineTime");
  }
  if (g_gatt != nullptr && g_gatt->caps().battery && g_bas_inited) {
    const std::uint8_t pct = slate::battery::percent();
    if (pct <= 100u) {
      (void)ble_svc_bas_battery_level_set(pct);
    }
  }

  // Services are registered in start_stack, before the host runs (N-16) —
  // ble_hs_start() calls ble_gatts_start() and only then fires this sync
  // callback, so anything queued here would never enter the attribute table.
  //
  // Self-check: by now the table is built, so ask the host whether the Slate
  // service is actually in it rather than assuming. State 96 means it is
  // not — which is exactly the condition a central reports as "service not
  // found", and it distinguishes a firmware fault from a stale GATT cache
  // on the phone.
  {
    std::uint16_t svc_handle = 0u;
    const int rc_find = ble_gatts_find_svc(&uuid_svc.u, &svc_handle);
    if (rc_find != 0) {
      rtt::log(rtt::Level::Error, "BLE: Slate service NOT in GATT table");
      bringup_mark(96u, rc_find);
    } else {
      rtt::write("BLE: Slate service registered, handle=0x");
      rtt::write_hex(svc_handle);
      rtt::write_line("");
    }
  }

  // Advertise Slate 128-bit service UUID so companions can filter without connect.
  // When HRS is on, also advertise uuid16 0x180D — move the name to the scan
  // response so the primary ADV PDU still fits in 31 bytes (InfiniTime pattern).
  struct ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = &uuid_svc;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  const bool hrs_adv = g_gatt != nullptr && g_gatt->caps().heart_rate;
  if (hrs_adv) {
    fields.uuids16 = &uuid_hrs_svc;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
  } else {
    fields.name = (uint8_t*)"Slate";
    fields.name_len = 5;
    fields.name_is_complete = 1;
  }
  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    rtt::log(rtt::Level::Error, "BLE: adv_set_fields failed");
    bringup_mark(92u, rc);
    return;
  }
  if (hrs_adv) {
    struct ble_hs_adv_fields rsp = {};
    rsp.name = (uint8_t*)"Slate";
    rsp.name_len = 5;
    rsp.name_is_complete = 1;
    (void)ble_gap_adv_rsp_set_fields(&rsp);
  }

  struct ble_gap_adv_params adv = {};
  adv.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
  rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &adv,
                         gap_event, nullptr);
  if (rc != 0) {
    rtt::log(rtt::Level::Error, "BLE: adv_start failed");
    bringup_mark(93u, rc);
    return;
  }
  bringup_mark(7u);
  rtt::log(rtt::Level::Info, "BLE: advertising Slate service");
#if defined(SLATE_BISECT_STAGE) && (SLATE_BISECT_STAGE == 3)
  paint_stage3(boot_diag::kWhite);
#endif
}

}  // namespace

void bringup_snapshot(std::uint8_t* state, std::uint16_t* rc) {
  if (state != nullptr) *state = g_bringup_state;
  if (rc != nullptr) *rc = g_bringup_rc;
}

void start_stack(GattServer* gatt, SessionProfile profile) {
  bringup_mark(1u);
  g_gatt = gatt;
  g_profile = profile;
  if (gatt != nullptr) {
    gatt->set_tx_sink(tx_notify, nullptr);
  }

  slate::ota_slot::init();
  slate::dfu::Hooks dh;
  dh.erase_slot = &dfu_erase;
  dh.write_slot = &dfu_write;
  dh.write_pending_magic = &dfu_magic;
  dh.battery_percent = &dfu_batt;
  dh.charging = &dfu_chg;
  dh.reboot = &dfu_reboot;
  dh.notify_rsp = &dfu_notify;
  g_dfu.init(dh);

  nimble_port_init();

#if defined(NIMBLE_CFG_CONTROLLER) && (NIMBLE_CFG_CONTROLLER == 1)
  // nimble/controller/pkg.yml registers ble_ll_init() as a sysinit entry, and
  // nimble_port_init() never runs it because the standalone porting layer has
  // no sysinit. Skipping it leaves g_ble_ll_data.ll_evq.q as NULL BSS, so the
  // ll task faults in xQueueReceive as soon as the scheduler starts.
  ble_ll_init();
#endif
  bringup_mark(2u);

  // xQueueCreate returns NULL when the FreeRTOS heap is exhausted and the NPL
  // wrappers do not check. Catch that here rather than in the ll/host task.
  if (nimble_port_get_dflt_eventq() == nullptr ||
      nimble_port_get_dflt_eventq()->q == nullptr) {
    rtt::log(rtt::Level::Error, "BLE: default eventq alloc failed (heap)");
    boot_diag::fatal_paint_and_hang(boot_diag::kRed);
  }
#if defined(NIMBLE_CFG_CONTROLLER) && (NIMBLE_CFG_CONTROLLER == 1)
  if (g_ble_ll_data.ll_evq.q == nullptr) {
    rtt::log(rtt::Level::Error, "BLE: ll eventq alloc failed (heap)");
    boot_diag::fatal_paint_and_hang(boot_diag::kRed);
  }
#endif
  bringup_mark(3u);

  ble_hs_cfg.sync_cb = on_sync;
  // A host reset (controller error, HCI timeout) silently kills advertising —
  // record it for the sealed diag overlay; on_sync overwrites if it recovers.
  ble_hs_cfg.reset_cb = [](int reason) {
    rtt::log(rtt::Level::Error, "BLE: host reset");
    bringup_mark(94u, reason);
  };
  ble_svc_gap_init();
  ble_svc_gatt_init();
  if (gatt != nullptr && gatt->caps().battery) {
    ble_svc_bas_init();
    g_bas_inited = true;
  }
  if (gatt != nullptr && gatt->caps().device_info) {
    ble_svc_dis_init();
  }

  // Register Slate's own services HERE, alongside the stock ones and before
  // the host task starts (N-16). ble_hs_start() calls ble_gatts_start(),
  // which is what actually builds the attribute table from everything queued
  // by ble_gatts_add_svcs(); the sync callback runs afterwards. Registering
  // in on_sync silently queued the services and never registered them, so
  // the Slate service (and the DFU service beside it) were advertised but
  // absent from the GATT table — centrals reported "service not found".
  // InfiniTime likewise registers everything up front and starts GATT
  // explicitly before use.
  {
    const int rc_cfg = ble_gatts_count_cfg(g_svcs);
    if (rc_cfg != 0) {
      rtt::log(rtt::Level::Error, "BLE: gatts_count_cfg failed");
      bringup_mark(90u, rc_cfg);
    } else {
      const int rc_add = ble_gatts_add_svcs(g_svcs);
      if (rc_add != 0) {
        rtt::log(rtt::Level::Error, "BLE: gatts_add_svcs failed");
        bringup_mark(91u, rc_add);
      }
    }
  }

  nimble_port_freertos_init(host_task);
  bringup_mark(4u);
  rtt::log(rtt::Level::Info, "BLE: NimBLE host started (Slate + BAS/DIS + legacy DFU)");
}

void update_battery_level(std::uint8_t percent) {
  if (g_gatt == nullptr || !g_gatt->caps().battery) {
    return;
  }
  if (!g_bas_inited) {
    return;
  }
  if (percent > 100u) {
    return;  // never publish kPercentUnknown on BAS
  }
  (void)ble_svc_bas_battery_level_set(percent);
}

void set_hrs_cccd_hook(HrsCccdFn fn, void* ctx) {
  g_hrs_cccd = fn;
  g_hrs_cccd_ctx = ctx;
}

void update_heart_rate(std::uint8_t bpm) {
  g_hrs_bpm = bpm;
  if (!g_hrs_notify || g_attr_hrm == 0 ||
      g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  if (g_gatt == nullptr || !g_gatt->caps().heart_rate) {
    return;
  }
  std::uint8_t buf[2] = {0u, bpm};
  struct os_mbuf* om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
  if (om) {
    (void)ble_gatts_notify_custom(g_conn_handle, g_attr_hrm, om);
  }
}

bool central_connected() { return g_conn_handle != BLE_HS_CONN_HANDLE_NONE; }

bool dfu_busy() { return g_dfu.state() != slate::dfu::State::Idle; }

void negotiate_now() {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  run_negotiate();
}

bool request_conn_interval(std::uint16_t interval_units) {
  if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }
  // Keep profile enum roughly aligned for STATUS reads.
  if (interval_units >= 200u) {
    g_profile = SessionProfile::Ambient;
  } else if (interval_units <= 12u) {
    g_profile = SessionProfile::Streaming;
  } else {
    g_profile = SessionProfile::Active;
  }
  return hook_conn_params(interval_units, 0u, 400u);
}

}  // namespace ble

#endif  // SLATE_HAS_NIMBLE
