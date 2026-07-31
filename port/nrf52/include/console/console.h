#pragma once
/* Stub Mynewt console for NimBLE host extras. */
static inline int console_printf(const char* fmt, ...) {
  (void)fmt;
  return 0;
}
#define console_write(...) ((void)0)
#define console_blocking_mode() ((void)0)
#define console_echo(...) ((void)0)
