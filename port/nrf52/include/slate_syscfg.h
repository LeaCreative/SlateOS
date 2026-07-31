#pragma once

// Forced via -include before any NimBLE header. Overrides porting/nimble defaults
// (those use #ifndef MYNEWT_VAL_*) for Slate on nRF52832.

#include "slate_ble_ll_syscfg.h"

#ifndef NIMBLE_NPL_OS_EXTRA_INCLUDE
#define NIMBLE_NPL_OS_EXTRA_INCLUDE "nrf.h"
#endif

#undef MYNEWT_VAL_BSP_SIMULATED
#define MYNEWT_VAL_BSP_SIMULATED (0)

#undef MYNEWT_VAL_MSYS_1_BLOCK_COUNT
#define MYNEWT_VAL_MSYS_1_BLOCK_COUNT (6)

#undef MYNEWT_VAL_MSYS_1_BLOCK_SIZE
#define MYNEWT_VAL_MSYS_1_BLOCK_SIZE (292)

#undef MYNEWT_VAL_BLE_MAX_CONNECTIONS
#define MYNEWT_VAL_BLE_MAX_CONNECTIONS (1)

#undef MYNEWT_VAL_BLE_TRANSPORT_ACL_COUNT
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_COUNT (4)

#undef MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_HS_COUNT
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_HS_COUNT (4)

#undef MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT (4)

#undef MYNEWT_VAL_BLE_TRANSPORT_EVT_COUNT
#define MYNEWT_VAL_BLE_TRANSPORT_EVT_COUNT (3)

// nRF52832 does not support BLE ISO (LE Audio / BIG). Disabling the ISO
// transport pool recovers ~3.4 KB of BSS that would otherwise be wasted.
#undef MYNEWT_VAL_BLE_TRANSPORT_ISO_COUNT
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_COUNT (0)
#undef MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_HS_COUNT
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_HS_COUNT (0)
#undef MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_LL_COUNT
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_LL_COUNT (0)

#undef MYNEWT_VAL_BLE_STORE_MAX_BONDS
#define MYNEWT_VAL_BLE_STORE_MAX_BONDS (0)

#undef MYNEWT_VAL_BLE_STORE_MAX_CCCDS
#define MYNEWT_VAL_BLE_STORE_MAX_CCCDS (4)

#undef MYNEWT_VAL_BLE_SM_LEGACY
#define MYNEWT_VAL_BLE_SM_LEGACY (0)

#undef MYNEWT_VAL_BLE_SM_SC
#define MYNEWT_VAL_BLE_SM_SC (0)

#undef MYNEWT_VAL_BLE_HW_WHITELIST_ENABLE
#define MYNEWT_VAL_BLE_HW_WHITELIST_ENABLE (0)

#undef MYNEWT_VAL_BLE_ATT_PREFERRED_MTU
#define MYNEWT_VAL_BLE_ATT_PREFERRED_MTU (247)

#undef MYNEWT_VAL_MCU_TARGET__nRF52832
#define MYNEWT_VAL_MCU_TARGET__nRF52832 (1)

// Mynewt supplies this via os/os.h; the standalone porting layer leaves it in
// os/util.h, which ble_ll_adv.c does not include.
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
