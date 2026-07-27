// NimBLE + FreeRTOS glue for Slate GATT (§4.1) and post-connect negotiation.
// Compiled only when SLATE_HAS_NIMBLE=1 and the Apache NimBLE tree is on the
// include/link path (see docs/ble.md).

#include "ble_gatt.hpp"
#include "ble_mbuf_stats.hpp"
#include "slate_uuids.hpp"
#include "rtt.hpp"
#include "sdp_frame.hpp"

#if defined(SLATE_HAS_NIMBLE) && (SLATE_HAS_NIMBLE == 1)

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/bas/ble_svc_bas.h"
#include "services/dis/ble_svc_dis.h"

#include <cstring>

namespace ble {
namespace {

GattServer* g_gatt = nullptr;
SessionProfile g_profile = SessionProfile::Active;
std::uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
std::uint16_t g_attr_tx = 0;
std::uint16_t g_attr_status = 0;

int gap_event(struct ble_gap_event* event, void* arg);

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
  return ble_gap_set_prefered_le_phy(
             g_conn_handle,
             BLE_GAP_LE_PHY_2M_MASK,
             BLE_GAP_LE_PHY_2M_MASK,
             BLE_GAP_LE_PHY_CODED_ANY) == 0;
}

void hook_read_phy(std::uint8_t* tx, std::uint8_t* rx) {
  if (tx) *tx = 2u;
  if (rx) *rx = 2u;
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

static const struct ble_gatt_svc_def g_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(
            0xfb, 0xac, 0x79, 0xe9, 0x00, 0x00, 0xfa, 0x44,
            0xa9, 0x62, 0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = BLE_UUID128_DECLARE(
                        0xfb, 0xac, 0x79, 0xe9, 0x01, 0x00, 0xfa, 0x44,
                        0xa9, 0x62, 0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3),
                    .access_cb = rx_access,
                    .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    .uuid = BLE_UUID128_DECLARE(
                        0xfb, 0xac, 0x79, 0xe9, 0x02, 0x00, 0xfa, 0x44,
                        0xa9, 0x62, 0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3),
                    .access_cb = tx_access,
                    .val_handle = &g_attr_tx,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                },
                {
                    .uuid = BLE_UUID128_DECLARE(
                        0xfb, 0xac, 0x79, 0xe9, 0x03, 0x00, 0xfa, 0x44,
                        0xa9, 0x62, 0xe9, 0x6e, 0x4c, 0xf0, 0x78, 0xf3),
                    .access_cb = status_access,
                    .val_handle = &g_attr_status,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                },
                {0},
            },
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
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      rtt::log(rtt::Level::Info, "BLE: disconnected");
      break;
    case BLE_GAP_EVENT_MTU:
      rtt::write("BLE: MTU event mtu=0x");
      rtt::write_hex(event->mtu.value);
      rtt::write_line("");
      break;
    default:
      break;
  }
  return 0;
}

void on_sync() {
  ble_svc_gap_device_name_set("Slate");
  ble_svc_dis_manufacturer_name_set("Slate");
  ble_svc_dis_model_number_set("PineTime");
  ble_svc_bas_battery_level_set(100);

  int rc = ble_gatts_count_cfg(g_svcs);
  if (rc != 0) {
    rtt::log(rtt::Level::Error, "BLE: gatts_count_cfg failed");
    return;
  }
  rc = ble_gatts_add_svcs(g_svcs);
  if (rc != 0) {
    rtt::log(rtt::Level::Error, "BLE: gatts_add_svcs failed");
    return;
  }

  struct ble_gap_adv_params adv = {};
  adv.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv, gap_event,
                    nullptr);
  rtt::log(rtt::Level::Info, "BLE: advertising Slate service");
}

void host_task(void*) {
  nimble_port_run();
}

}  // namespace

void start_stack(GattServer* gatt, SessionProfile profile) {
  g_gatt = gatt;
  g_profile = profile;
  if (gatt != nullptr) {
    gatt->set_tx_sink(tx_notify, nullptr);
  }

  nimble_port_init();
  ble_hs_cfg.sync_cb = on_sync;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_bas_init();
  ble_svc_dis_init();
  // Heart Rate + CTS client: registered when sensor/time paths land (M7/M10).
  // Capability flags already exposed via GattCaps for negotiation.

  nimble_port_freertos_init(host_task);
  rtt::log(rtt::Level::Info, "BLE: NimBLE host started");
}

}  // namespace ble

#endif  // SLATE_HAS_NIMBLE
