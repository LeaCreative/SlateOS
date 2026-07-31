#pragma once

#include <stdarg.h>

/* Silent NPL logging — no console on Slate. Must expand to MODULE_LEVEL names
 * (e.g. BLE_EATT_LOG_ERROR) via BLE_NPL_LOG_MODULE, matching stock NimBLE. */
#define BLE_NPL_LOG_IMPL(lvl)                                          \
  static inline void _BLE_NPL_LOG_CAT(BLE_NPL_LOG_MODULE,               \
                                     _BLE_NPL_LOG_CAT(_, lvl))(         \
      const char* fmt, ...) {                                          \
    (void)fmt;                                                         \
  }
