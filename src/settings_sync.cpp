#include "settings_sync.hpp"

#include "sdp_opcodes.hpp"

namespace slate {
namespace settings_sync {

std::size_t encode(const Payload& p, std::uint8_t* out, std::size_t cap) {
  if (out == nullptr || cap < kPayloadBytes) {
    return 0u;
  }
  out[0] = sdp::control_op::SETTINGS_SYNC;
  out[1] = kWireVersion;
  out[2] = static_cast<std::uint8_t>(p.revision & 0xFFu);
  out[3] = static_cast<std::uint8_t>((p.revision >> 8) & 0xFFu);
  out[4] = static_cast<std::uint8_t>((p.revision >> 16) & 0xFFu);
  out[5] = static_cast<std::uint8_t>((p.revision >> 24) & 0xFFu);
  out[6] = p.tilt_enabled ? 1u : 0u;
  out[7] = p.wake_seconds;
  out[8] = p.face_show_steps ? 1u : 0u;
  out[9] = p.face_show_diag ? 1u : 0u;
  return kPayloadBytes;
}

bool decode(const std::uint8_t* msg, std::size_t len, Payload* out) {
  if (msg == nullptr || out == nullptr || len < kPayloadBytes) {
    return false;
  }
  if (msg[0] != sdp::control_op::SETTINGS_SYNC) {
    return false;
  }
  // Unknown future versions are rejected rather than guessed at. A settings
  // message is small and rare; misreading one silently changes what the watch
  // does, which is worse than ignoring it until both ends agree.
  if (msg[1] != kWireVersion) {
    return false;
  }
  out->revision = static_cast<std::uint32_t>(msg[2]) |
                  (static_cast<std::uint32_t>(msg[3]) << 8) |
                  (static_cast<std::uint32_t>(msg[4]) << 16) |
                  (static_cast<std::uint32_t>(msg[5]) << 24);
  out->tilt_enabled = msg[6] ? 1u : 0u;
  out->wake_seconds = msg[7];
  out->face_show_steps = msg[8] ? 1u : 0u;
  out->face_show_diag = msg[9] ? 1u : 0u;
  return true;
}

bool differs(const Payload& a, const Payload& b) {
  return a.tilt_enabled != b.tilt_enabled || a.wake_seconds != b.wake_seconds ||
         a.face_show_steps != b.face_show_steps ||
         a.face_show_diag != b.face_show_diag;
}

bool should_apply(const Payload& current, const Payload& incoming,
                  bool self_is_watch) {
  if (incoming.revision > current.revision) {
    return true;
  }
  if (incoming.revision < current.revision) {
    return false;
  }
  // Equal revisions. Identical content is a no-op either way.
  if (!differs(current, incoming)) {
    return false;
  }
  // A real conflict: both sides edited without seeing each other. The watch
  // wins, so the phone yields. Both ends run this same function and reach the
  // same verdict from their own point of view, which is what stops them
  // overwriting each other forever.
  return !self_is_watch;
}

std::uint32_t next_revision(std::uint32_t local_rev, std::uint32_t highest_seen) {
  const std::uint32_t base = local_rev > highest_seen ? local_rev : highest_seen;
  // Saturate rather than wrap. A wrap would make an ancient revision look
  // newer than a current one and silently resurrect old settings; at one
  // increment per user edit this is unreachable in practice anyway.
  if (base == 0xFFFFFFFFu) {
    return base;
  }
  return base + 1u;
}

void apply_to(const Payload& p, local::Settings* s) {
  if (s == nullptr) {
    return;
  }
  s->tilt_enabled = p.tilt_enabled ? 1u : 0u;
  s->wake_seconds = p.wake_seconds;
  s->face_show_steps = p.face_show_steps ? 1u : 0u;
  s->face_show_diag = p.face_show_diag ? 1u : 0u;
}

Payload from(const local::Settings& s, std::uint32_t revision) {
  Payload p;
  p.revision = revision;
  p.tilt_enabled = s.tilt_enabled ? 1u : 0u;
  p.wake_seconds = s.wake_seconds;
  p.face_show_steps = s.face_show_steps ? 1u : 0u;
  p.face_show_diag = s.face_show_diag ? 1u : 0u;
  return p;
}

}  // namespace settings_sync
}  // namespace slate
