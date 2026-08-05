#pragma once

#include "sdp_opcodes.hpp"
#include "session_profiles.hpp"

#include <cstddef>
#include <cstdint>

// SDP session state machine (§4.6). Host-safe: no BLE/display deps — inject I/O.

namespace slate {
namespace session {

// HELLO_OFFER worst-case encoded size — keep in sync with the layout in
// Manager::encode_hello_offer. Fixed fields: op, fw×3, proto u16, w u16,
// h u16, opcode bitmap, free_dl u16, profile_count, asset_pack_count, flags.
// Per profile: id, name_len, name[≤kMaxProfileNameLen], backlight,
// interval u16.
constexpr std::size_t kHelloOfferFixedBytes =
    1u + 3u + 2u + 2u + 2u + sdp::kOpcodeBitmapBytes + 2u + 1u + 1u + 1u;
constexpr std::size_t kHelloOfferPerProfileBytes =
    2u + sdp::kMaxProfileNameLen + 1u + 2u;
constexpr std::size_t kHelloOfferMaxBytes =
    kHelloOfferFixedBytes + profile::kCatalogCount * kHelloOfferPerProfileBytes;
// Encode/TX buffer. Catalog growth must fail the build here, not overflow a
// stack buffer on the watch (N-6).
constexpr std::size_t kHelloOfferBufBytes = 160u;
static_assert(kHelloOfferMaxBytes <= kHelloOfferBufBytes,
              "profile catalog outgrew the HELLO_OFFER buffer — "
              "bump kHelloOfferBufBytes deliberately");

enum class State : std::uint8_t {
  Disconnected = 0,
  Connected,   // link up; offering / waiting for HELLO_ACCEPT
  Ready,       // negotiated; remote depth 0 (watch face)
  Active,      // remote screen(s) present
  Idle,        // negotiated but heartbeats stale (overlay); depth may still >0
};

enum class PendingDisplay : std::uint8_t {
  Replace = 0,  // default: DISPLAY replaces top (or becomes depth 1)
  Push,
};

enum class ApplyListResult : std::uint8_t {
  Ok = 0,
  Reject,
};

struct Hooks {
  // Send a complete SDP message on `channel`. Return false if link cannot TX.
  bool (*send)(std::uint8_t channel, const std::uint8_t* msg, std::size_t len,
               void* ctx) = nullptr;

  // Apply a validated display list as the current remote top. Return Ok/Reject.
  ApplyListResult (*apply_list)(const std::uint8_t* data, std::size_t len,
                                void* ctx) = nullptr;

  // Reveal local watch face; clear remote pixels. `stale` draws staleness cue.
  void (*show_watch_face)(bool stale, void* ctx) = nullptr;

  // Apply firmware profile parameters (backlight + conn interval request).
  void (*apply_profile)(const profile::Desc& desc, void* ctx) = nullptr;

  void (*log)(const char* msg, void* ctx) = nullptr;

  void* ctx = nullptr;
};

class Manager {
public:
  void init(const Hooks& hooks);

  void set_diag_available(bool on) { diag_available_ = on; }

  State state() const { return state_; }

  /** Display lists dropped before apply_list ran (wrong state / no hook). */
  std::uint16_t display_drops() const { return display_drops_; }
  std::uint8_t remote_depth() const { return remote_depth_; }
  bool stale() const { return stale_; }
  std::uint8_t profile_id() const { return profile_id_; }
  bool same_phone_as_last() const { return same_phone_; }
  const std::uint8_t* phone_id() const { return phone_id_; }

  // Link lifecycle.
  void on_link_up(std::uint32_t now_ms);
  void on_link_down(std::uint32_t now_ms);

  // Completed CONTROL (ch0) or DISPLAY (ch1) message.
  void on_control(const std::uint8_t* msg, std::size_t len, std::uint32_t now_ms);
  void on_display(const std::uint8_t* msg, std::size_t len, std::uint32_t now_ms);

  // Call ~every 100–500 ms while linked.
  void tick(std::uint32_t now_ms);

  // Local BACK within a remote screen → emit SESSION_END USER_BACK or pop.
  // Returns true if an INPUT event was sent.
  bool local_back(std::uint32_t now_ms);

  std::uint16_t free_dl_bytes() const;

  // Build HELLO_OFFER into out; returns bytes written (0 on overflow).
  static std::size_t encode_hello_offer(std::uint8_t* out, std::size_t cap,
                                        bool diag_available);

  static void fill_opcode_bitmap(std::uint8_t bitmap[sdp::kOpcodeBitmapBytes]);

private:
  void set_state(State s);
  void send_hello_offer();
  void send_credit();
  void send_profile_ack(std::uint8_t id, std::uint8_t status);
  void send_session_end(std::uint8_t reason);
  void send_input(const std::uint8_t* msg, std::size_t len);
  bool parse_hello_accept(const std::uint8_t* msg, std::size_t len);
  void apply_selected_profile(std::uint8_t id);
  void pop_remote_all(std::uint8_t reason, bool notify);
  void enter_stale();
  void clear_phone();

  Hooks hooks_{};
  State state_ = State::Disconnected;
  std::uint16_t display_drops_ = 0u;
  PendingDisplay pending_ = PendingDisplay::Replace;
  std::uint8_t remote_depth_ = 0u;
  std::uint8_t profile_id_ = profile::kIdActive;
  std::uint8_t phone_id_[sdp::kPhoneIdBytes] = {};
  std::uint8_t last_phone_id_[sdp::kPhoneIdBytes] = {};
  bool have_phone_ = false;
  bool have_last_phone_ = false;
  bool same_phone_ = false;
  bool stale_ = false;
  bool diag_available_ = false;
  std::uint32_t last_hb_ms_ = 0u;
  std::uint32_t link_up_ms_ = 0u;
};

}  // namespace session
}  // namespace slate
