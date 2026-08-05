#include "session.hpp"

#include "sdp_frame.hpp"

#include <cstring>

namespace slate {
namespace session {
namespace {

void put_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

bool ids_equal(const std::uint8_t* a, const std::uint8_t* b) {
  return std::memcmp(a, b, sdp::kPhoneIdBytes) == 0;
}

}  // namespace

void Manager::fill_opcode_bitmap(std::uint8_t bitmap[sdp::kOpcodeBitmapBytes]) {
  std::memset(bitmap, 0, sdp::kOpcodeBitmapBytes);
  auto set = [&](std::uint8_t op) {
    bitmap[op >> 3] =
        static_cast<std::uint8_t>(bitmap[op >> 3] | (1u << (op & 7u)));
  };
  using namespace sdp::op;
  set(CLEAR); set(SET_PALETTE); set(RECT); set(RECT_ROUND); set(LINE);
  set(CIRCLE); set(ARC); set(POLYLINE); set(CLIP_RECT); set(CLIP_CLEAR);
  set(TEXT); set(TEXT_BOX); set(ICON); set(IMAGE);
  set(PROGRESS_BAR); set(PROGRESS_ARC);
  set(BEGIN_ELEM); set(END_ELEM); set(SCROLL_REGION);
  set(PATCH); set(PATCH_REF);
  set(HAPTIC); set(BACKLIGHT);
  set(COMMIT); set(RETAIN);
  // Extension range advertised as skippable presence — leave unset until used.
}

std::size_t Manager::encode_hello_offer(std::uint8_t* out, std::size_t cap,
                                        bool diag_available) {
  // Layout:
  //  op, fw_maj,min,patch, proto_u16, w_u16, h_u16,
  //  opcode_bitmap[32], free_dl_u16,
  //  profile_count, {id, name_len, name...}*,
  //  asset_pack_count (=0), flags
  if (out == nullptr) {
    return 0u;
  }
  // Every write is capacity-checked (N-6): overflow flips `ok` and the whole
  // encode returns 0, like the local-UI writer W — never a silent truncation
  // or an unbounded write past tmp.
  std::uint8_t tmp[kHelloOfferBufBytes];
  std::size_t n = 0u;
  bool ok = true;
  auto push = [&](std::uint8_t b) {
    if (n < sizeof(tmp)) {
      tmp[n++] = b;
    } else {
      ok = false;
    }
  };
  auto push_u16 = [&](std::uint16_t v) {
    push(static_cast<std::uint8_t>(v & 0xFFu));
    push(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  };
  auto push_bytes = [&](const std::uint8_t* p, std::size_t len) {
    for (std::size_t i = 0u; i < len; ++i) {
      push(p[i]);
    }
  };

  push(sdp::control_op::HELLO_OFFER);
  push(sdp::kFwMajor);
  push(sdp::kFwMinor);
  push(sdp::kFwPatch);
  push_u16(sdp::kProtocolVersion);
  push_u16(static_cast<std::uint16_t>(sdp::kDisplaySize));
  push_u16(static_cast<std::uint16_t>(sdp::kDisplaySize));

  std::uint8_t bitmap[sdp::kOpcodeBitmapBytes];
  fill_opcode_bitmap(bitmap);
  push_bytes(bitmap, sizeof(bitmap));

  push_u16(static_cast<std::uint16_t>(sdp::kMaxListBytes));

  push(profile::kCatalogCount);
  for (std::uint8_t i = 0u; i < profile::kCatalogCount; ++i) {
    const profile::Desc& d = profile::kCatalog[i];
    push(d.id);
    std::size_t namelen = 0u;
    while (d.name[namelen] != '\0' && namelen < sdp::kMaxProfileNameLen) {
      ++namelen;
    }
    push(static_cast<std::uint8_t>(namelen));
    for (std::size_t c = 0u; c < namelen; ++c) {
      push(static_cast<std::uint8_t>(d.name[c]));
    }
    push(d.backlight);
    push_u16(d.interval_units);
  }

  push(0u);  // asset_pack_count — none until M11
  push(diag_available ? 0x01u : 0x00u);

  if (!ok || n > cap) {
    return 0u;
  }
  std::memcpy(out, tmp, n);
  return n;
}

void Manager::init(const Hooks& hooks) {
  hooks_ = hooks;
  state_ = State::Disconnected;
  pending_ = PendingDisplay::Replace;
  remote_depth_ = 0u;
  profile_id_ = profile::kIdActive;
  clear_phone();
  have_last_phone_ = false;
  same_phone_ = false;
  stale_ = false;
  diag_available_ = false;
  last_hb_ms_ = 0u;
  link_up_ms_ = 0u;
}

void Manager::set_state(State s) { state_ = s; }

void Manager::clear_phone() {
  std::memset(phone_id_, 0, sdp::kPhoneIdBytes);
  have_phone_ = false;
}

std::uint16_t Manager::free_dl_bytes() const {
  // Single retained buffer — free is full capacity when depth==0, else 0 until
  // credit accounting grows (M8). Enough for negotiation reporting.
  return remote_depth_ == 0u
             ? static_cast<std::uint16_t>(sdp::kMaxListBytes)
             : 0u;
}

void Manager::send_input(const std::uint8_t* msg, std::size_t len) {
  if (hooks_.send == nullptr || msg == nullptr || len == 0u) {
    return;
  }
  (void)hooks_.send(sdp::frame::kChanInput, msg, len, hooks_.ctx);
}

void Manager::send_session_end(std::uint8_t reason) {
  const std::uint8_t msg[] = {sdp::input_op::SESSION_END, reason};
  send_input(msg, sizeof(msg));
}

void Manager::send_credit() {
  if (hooks_.send == nullptr) {
    return;
  }
  std::uint8_t msg[3];
  msg[0] = sdp::control_op::CREDIT;
  put_u16(msg + 1, free_dl_bytes());
  (void)hooks_.send(sdp::frame::kChanControl, msg, sizeof(msg), hooks_.ctx);
}

void Manager::send_profile_ack(std::uint8_t id, std::uint8_t status) {
  if (hooks_.send == nullptr) {
    return;
  }
  const std::uint8_t msg[] = {sdp::control_op::PROFILE_ACK, id, status};
  (void)hooks_.send(sdp::frame::kChanControl, msg, sizeof(msg), hooks_.ctx);
}

void Manager::send_hello_offer() {
  if (hooks_.send == nullptr) {
    return;
  }
  std::uint8_t buf[kHelloOfferBufBytes];
  const std::size_t n = encode_hello_offer(buf, sizeof(buf), diag_available_);
  if (n > 0u) {
    (void)hooks_.send(sdp::frame::kChanControl, buf, n, hooks_.ctx);
  }
}

void Manager::apply_selected_profile(std::uint8_t id) {
  const profile::Desc* d = profile::find(id);
  if (d == nullptr) {
    return;
  }
  profile_id_ = id;
  if (hooks_.apply_profile) {
    hooks_.apply_profile(*d, hooks_.ctx);
  }
}

void Manager::on_link_up(std::uint32_t now_ms) {
  link_up_ms_ = now_ms;
  last_hb_ms_ = now_ms;
  stale_ = false;
  remote_depth_ = 0u;
  pending_ = PendingDisplay::Replace;
  clear_phone();
  same_phone_ = false;
  set_state(State::Connected);
  if (hooks_.show_watch_face) {
    hooks_.show_watch_face(false, hooks_.ctx);
  }
  send_hello_offer();
  if (hooks_.log) {
    hooks_.log("session: link up → Connected, HELLO_OFFER", hooks_.ctx);
  }
}

void Manager::on_link_down(std::uint32_t now_ms) {
  (void)now_ms;
  if (state_ != State::Disconnected) {
    // Cannot TX SESSION_END reliably; record reason locally.
    if (have_phone_) {
      std::memcpy(last_phone_id_, phone_id_, sdp::kPhoneIdBytes);
      have_last_phone_ = true;
    }
  }
  remote_depth_ = 0u;
  stale_ = false;
  clear_phone();
  set_state(State::Disconnected);
  if (hooks_.show_watch_face) {
    hooks_.show_watch_face(false, hooks_.ctx);
  }
  if (hooks_.log) {
    hooks_.log("session: link down → Disconnected", hooks_.ctx);
  }
}

bool Manager::parse_hello_accept(const std::uint8_t* msg, std::size_t len) {
  // HELLO_ACCEPT: op, profile_id, phone_id[8], host_ver_len, host_ver...
  if (len < 1u + 1u + sdp::kPhoneIdBytes + 1u) {
    return false;
  }
  if (msg[0] != sdp::control_op::HELLO_ACCEPT) {
    return false;
  }
  const std::uint8_t profile = msg[1];
  if (profile::find(profile) == nullptr) {
    return false;
  }
  const std::uint8_t ver_len = msg[1u + 1u + sdp::kPhoneIdBytes];
  if (static_cast<std::size_t>(1u + 1u + sdp::kPhoneIdBytes + 1u + ver_len) > len) {
    return false;
  }

  std::memcpy(phone_id_, msg + 2, sdp::kPhoneIdBytes);
  have_phone_ = true;
  same_phone_ = have_last_phone_ && ids_equal(phone_id_, last_phone_id_);
  apply_selected_profile(profile);
  return true;
}

void Manager::pop_remote_all(std::uint8_t reason, bool notify) {
  remote_depth_ = 0u;
  stale_ = false;
  pending_ = PendingDisplay::Replace;
  if (notify) {
    send_session_end(reason);
  }
  if (hooks_.show_watch_face) {
    hooks_.show_watch_face(false, hooks_.ctx);
  }
  if (state_ == State::Active || state_ == State::Idle) {
    set_state(State::Ready);
  }
  send_credit();
}

void Manager::enter_stale() {
  if (stale_) {
    return;
  }
  stale_ = true;
  if (remote_depth_ > 0u) {
    set_state(State::Idle);
    if (hooks_.show_watch_face) {
      // Keep remote pixels conceptually; overlay via flag for renderer later.
      hooks_.show_watch_face(true, hooks_.ctx);
    }
  }
  if (hooks_.log) {
    hooks_.log("session: heartbeat stale overlay", hooks_.ctx);
  }
}

void Manager::on_control(const std::uint8_t* msg, std::size_t len,
                         std::uint32_t now_ms) {
  if (msg == nullptr || len == 0u) {
    return;
  }
  const std::uint8_t op = msg[0];

  switch (op) {
    case sdp::control_op::HELLO_ACCEPT: {
      if (state_ != State::Connected) {
        return;
      }
      if (!parse_hello_accept(msg, len)) {
        if (hooks_.log) {
          hooks_.log("session: HELLO_ACCEPT rejected", hooks_.ctx);
        }
        // Stay Connected; phone may retry or disconnect.
        return;
      }
      last_hb_ms_ = now_ms;
      stale_ = false;
      remote_depth_ = 0u;
      set_state(State::Ready);
      send_credit();
      if (hooks_.log) {
        hooks_.log(same_phone_ ? "session: Ready (same phone)"
                               : "session: Ready (new/other phone)",
                   hooks_.ctx);
      }
      break;
    }
    case sdp::control_op::HELLO_REJECT: {
      if (state_ == State::Connected) {
        if (hooks_.log) {
          hooks_.log("session: HELLO_REJECT — remain Connected", hooks_.ctx);
        }
        // Negotiation failure: do not enter Ready.
      }
      break;
    }
    case sdp::control_op::HEARTBEAT: {
      if (state_ == State::Ready || state_ == State::Active ||
          state_ == State::Idle) {
        last_hb_ms_ = now_ms;
        if (stale_) {
          stale_ = false;
          if (remote_depth_ > 0u) {
            set_state(State::Active);
          } else {
            set_state(State::Ready);
          }
          if (hooks_.show_watch_face && remote_depth_ == 0u) {
            hooks_.show_watch_face(false, hooks_.ctx);
          }
        }
      }
      break;
    }
    case sdp::control_op::SET_PROFILE: {
      if (len < 2u) {
        return;
      }
      if (state_ != State::Ready && state_ != State::Active &&
          state_ != State::Idle) {
        return;
      }
      const std::uint8_t id = msg[1];
      if (profile::find(id) == nullptr) {
        send_profile_ack(id, 1u);  // unknown — phone cannot invent profiles
        return;
      }
      apply_selected_profile(id);
      send_profile_ack(id, 0u);
      break;
    }
    case sdp::control_op::SCREEN_PUSH:
      if (state_ == State::Ready || state_ == State::Active ||
          state_ == State::Idle) {
        pending_ = PendingDisplay::Push;
      }
      break;
    case sdp::control_op::SCREEN_REPLACE:
      if (state_ == State::Ready || state_ == State::Active ||
          state_ == State::Idle) {
        pending_ = PendingDisplay::Replace;
      }
      break;
    case sdp::control_op::SCREEN_POP: {
      if (remote_depth_ == 0u) {
        break;
      }
      --remote_depth_;
      pending_ = PendingDisplay::Replace;
      if (remote_depth_ == 0u) {
        stale_ = false;
        set_state(State::Ready);
        if (hooks_.show_watch_face) {
          hooks_.show_watch_face(false, hooks_.ctx);
        }
        send_credit();
      } else {
        // Lower screen not retained (RAM); mark stale until phone REPLACE/PUSH.
        enter_stale();
        send_credit();
      }
      break;
    }
    case sdp::control_op::GOODBYE:
      pop_remote_all(sdp::session_end_reason::PHONE_REQUEST, true);
      break;
    default:
      break;
  }
}

void Manager::on_display(const std::uint8_t* msg, std::size_t len,
                         std::uint32_t now_ms) {
  if (state_ != State::Ready && state_ != State::Active &&
      state_ != State::Idle) {
    // Silently discarding here made a wrong session state look identical to a
    // transport failure: apply_list never runs, so the dl counters stay at 0.
    ++display_drops_;
    return;
  }
  if (hooks_.apply_list == nullptr || msg == nullptr || len == 0u) {
    ++display_drops_;
    return;
  }

  PendingDisplay how = pending_;
  if (remote_depth_ == 0u) {
    how = PendingDisplay::Push;  // first remote screen is always a push
  }

  const ApplyListResult ar = hooks_.apply_list(msg, len, hooks_.ctx);
  if (ar != ApplyListResult::Ok) {
    return;
  }

  if (how == PendingDisplay::Push) {
    if (remote_depth_ < 7u) {
      ++remote_depth_;
    }
  } else if (remote_depth_ == 0u) {
    remote_depth_ = 1u;
  }

  pending_ = PendingDisplay::Replace;
  stale_ = false;
  last_hb_ms_ = now_ms;
  set_state(State::Active);
  // CREDIT after deferred apply (app task). Host never credits on enqueue.
  send_credit();
}

void Manager::tick(std::uint32_t now_ms) {
  if (state_ != State::Ready && state_ != State::Active &&
      state_ != State::Idle) {
    return;
  }
  // Unsigned elapsed: correct across u32 wrap when now_ms wraps at 2^32
  // (slate::time::mono_ms). Do not feed micros()/1000 here — that wrapped near
  // 4.29e6 ms and made (500 - 4294000) look like a multi-day stall.
  const std::uint32_t elapsed = now_ms - last_hb_ms_;
  const std::uint32_t misses = elapsed / sdp::kHeartbeatPeriodMs;
  if (misses >= sdp::kHeartbeatDropMisses) {
    if (hooks_.log) {
      hooks_.log("session: 5 missed heartbeats — pop remote", hooks_.ctx);
    }
    pop_remote_all(sdp::session_end_reason::TIMEOUT, true);
    return;
  }
  if (misses >= sdp::kHeartbeatStaleMisses) {
    enter_stale();
  }
}

bool Manager::local_back(std::uint32_t now_ms) {
  (void)now_ms;
  if (remote_depth_ == 0u) {
    return false;
  }
  // The watch pops its own screen stack rather than waiting for the phone to
  // do it. The caller still emits BACK on the wire so the companion can follow,
  // but the user must never depend on the phone being responsive to get back to
  // the watch face — a wedged or busy companion previously trapped the screen
  // with no local escape. The companion's own fallback only popped when its
  // stack held more than one app, so a single focused app was inescapable.
  --remote_depth_;
  if (remote_depth_ == 0u) {
    pending_ = PendingDisplay::Replace;
    if (hooks_.show_watch_face) {
      hooks_.show_watch_face(false, hooks_.ctx);
    }
  }
  return true;
}

}  // namespace session
}  // namespace slate
