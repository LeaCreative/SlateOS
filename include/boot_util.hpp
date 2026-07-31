#pragma once

#include <cstdint>

// MCUBoot primary-slot confirm.
//
// An unconfirmed image is reverted by MCUBoot on the next reset, which is the
// safety net that stops a broken build from stranding a sealed watch. Confirming
// therefore has to prove the one thing recovery depends on: that a phone can
// reach the GATT server to push the next DFU. Anything less — a timer, an uptime
// counter, a button press — would happily confirm an image whose radio never
// came up, leaving no way in short of opening the case.
//
// The bar is a central holding the link for kConfirmDwellMs. Deliberately *not*
// a completed Slate SDP handshake: nRF Connect, Gadgetbridge or the companion
// app all prove the radio equally well, and requiring the full handshake meant
// the user had to finish Android permissions and pairing before the image was
// safe. See docs/flash-sealed.md.

namespace slate {
namespace boot {

// A central must hold the connection this long before the image is confirmed,
// so a link that drops straight back out does not count.
constexpr std::uint32_t kConfirmDwellMs = 10000u;

// True if this boot still needs confirmation (test image / swap pending).
bool needs_confirm();

// Write IMAGE_OK in the primary trailer. Safe to call multiple times.
bool confirm_image();

// Feed the current link state on every app tick. Returns true on the tick that
// confirms, so the caller can clear its trial indicator.
bool tick_confirm(bool central_connected, std::uint32_t now_ms);

// Ms left of connected dwell before IMAGE_OK. 0 when confirmed / N/A.
// When trial and not (yet) timing a connection, returns kConfirmDwellMs.
std::uint32_t dwell_remaining_ms(bool central_connected, std::uint32_t now_ms);

// True when (now - since) has reached kConfirmDwellMs with u32 modular elapsed
// (requires now_ms from slate::time::mono_ms, wrap at 2^32).
inline bool dwell_satisfied(std::uint32_t now_ms, std::uint32_t since_ms) {
  return static_cast<std::uint32_t>(now_ms - since_ms) >= kConfirmDwellMs;
}

}  // namespace boot
}  // namespace slate
