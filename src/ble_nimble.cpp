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
slate::dfu::Engine g_dfu{};
std::uint8_t g_phy_tx = 1u;
std::uint8_t g_phy_rx = 1u;
NegotiatedParams g_last_nego{};
bool g_bas_inited = false;

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
    if (g_attr_status != 0 && g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
      struct os_mbuf* om =
          ble_hs_mbuf_from_flat(g_gatt->status_value(), g_gatt->status_len());
      if (om) {
        (void)ble_gatts_notify_custom(g_conn_handle, g_attr_status, om);
      }
    }
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

  // Confirm-on-BLE: only after a real central + negotiation.
  notify_session_up();
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

static const ble_uuid128_t uuid_svc = BLE_UUID128_INIT(
    0xfb, 0xac, 0x79, 0xe9, 0x00, 0x00, 0xfa, 0x44, 0xa9, 0x62, 0xe9, 0x6e,
    0x4c, 0xf0, 0x78, 0xf3);
static const ble_uuid128_t uuid_rx = BLE_UUID128_INIT(
    0xfb, 0xac, 0x79, 0xe9, 0x01, 0x00, 0xfa, 0x44, 0xa9, 0x62, 0xe9, 0x6e,
    0x4c, 0xf0, 0x78, 0xf3);
static const ble_uuid128_t uuid_tx = BLE_UUID128_INIT(
    0xfb, 0xac, 0x79, 0xe9, 0x02, 0x00, 0xfa, 0x44, 0xa9, 0x62, 0xe9, 0x6e,
    0x4c, 0xf0, 0x78, 0xf3);
static const ble_uuid128_t uuid_status = BLE_UUID128_INIT(
    0xfb, 0xac, 0x79, 0xe9, 0x03, 0x00, 0xfa, 0x44, 0xa9, 0x62, 0xe9, 0x6e,
    0x4c, 0xf0, 0x78, 0xf3);
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
    {0},
};

int gap_event(struct ble_gap_event* event, void* arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        g_conn_handle = event->connect.conn_handle;
        rtt::log(rtt::Level::Info, "BLE: connected — starting negotiation");
        run_negotiate();
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
      rtt::log(rtt::Level::Info, "BLE: disconnected");
      g_dfu.on_disconnect();
      notify_session_down();
      if (gap_adv::should_resume(gap_adv::EventKind::Disconnect)) {
        resume_advertising();
      }
      break;
    case BLE_GAP_EVENT_MTU:
      rtt::write("BLE: MTU event mtu=0x");
      rtt::write_hex(event->mtu.value);
      rtt::write_line("");
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

  int rc = ble_gatts_count_cfg(g_svcs);
  if (rc != 0) {
    rtt::log(rtt::Level::Error, "BLE: gatts_count_cfg failed");
    bringup_mark(90u, rc);
    return;
  }
  rc = ble_gatts_add_svcs(g_svcs);
  if (rc != 0) {
    rtt::log(rtt::Level::Error, "BLE: gatts_add_svcs failed");
    bringup_mark(91u, rc);
    return;
  }

  // Advertise Slate 128-bit service UUID so companions can filter without connect.
  struct ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = (uint8_t*)"Slate";
  fields.name_len = 5;
  fields.name_is_complete = 1;
  fields.uuids128 = &uuid_svc;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    rtt::log(rtt::Level::Error, "BLE: adv_set_fields failed");
    bringup_mark(92u, rc);
    return;
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

bool central_connected() { return g_conn_handle != BLE_HS_CONN_HANDLE_NONE; }

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
