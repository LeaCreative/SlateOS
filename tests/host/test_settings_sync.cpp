// Settings sync: the wire format and the merge rule.
//
// "Whichever side changed last wins" is one sentence and several ways to get
// wrong, and every way is quiet: a setting the user changed reverts a few
// seconds later, or the two ends overwrite each other forever. Both are far
// cheaper to find here than on a wrist.

#include "settings_sync.hpp"
#include "sdp_opcodes.hpp"

#include <cstdio>
#include <cstring>

namespace {

int g_fails = 0;

void expect(const char* name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    ++g_fails;
  } else {
    std::printf("ok   %s\n", name);
  }
}

using slate::settings_sync::Payload;
namespace ss = slate::settings_sync;

Payload make(std::uint32_t rev, std::uint8_t tilt, std::uint8_t wake,
             std::uint8_t steps, std::uint8_t diag = 1u,
             std::uint8_t hr = 0u) {
  Payload p;
  p.revision = rev;
  p.tilt_enabled = tilt;
  p.wake_seconds = wake;
  p.face_show_steps = steps;
  p.face_show_diag = diag;
  p.hr_enabled = hr;
  return p;
}

void test_round_trip() {
  const Payload in = make(0xDEADBEEFu, 1u, 45u, 0u);
  std::uint8_t buf[32] = {};
  const std::size_t n = ss::encode(in, buf, sizeof(buf));
  expect("encodes the documented length", n == ss::kPayloadBytes);
  expect("carries the SETTINGS_SYNC opcode",
         buf[0] == sdp::control_op::SETTINGS_SYNC);
  expect("carries the wire version", buf[1] == ss::kWireVersion);

  Payload out;
  expect("decodes", ss::decode(buf, n, &out));
  expect("revision survives", out.revision == in.revision);
  expect("tilt survives", out.tilt_enabled == in.tilt_enabled);
  expect("wake_seconds survives", out.wake_seconds == in.wake_seconds);
  expect("show steps survives", out.face_show_steps == in.face_show_steps);
  expect("show diag survives", out.face_show_diag == in.face_show_diag);
  expect("hr_enabled survives", out.hr_enabled == in.hr_enabled);
  expect("ui_chrome survives", out.ui_chrome == in.ui_chrome);
  expect("face_bright survives", out.face_bright == in.face_bright);
  expect("face_dim survives", out.face_dim == in.face_dim);
}

void test_decode_rejects_rubbish() {
  Payload out;
  std::uint8_t buf[32] = {};
  (void)ss::encode(make(1u, 1u, 20u, 1u), buf, sizeof(buf));

  expect("short buffer rejected", !ss::decode(buf, ss::kPayloadBytes - 1u, &out));

  std::uint8_t wrong_op[ss::kPayloadBytes];
  std::memcpy(wrong_op, buf, sizeof(wrong_op));
  wrong_op[0] = sdp::control_op::HEARTBEAT;
  expect("wrong opcode rejected", !ss::decode(wrong_op, sizeof(wrong_op), &out));

  // A future version must be ignored, not guessed at: misreading a settings
  // message silently changes what the watch does.
  std::uint8_t future[ss::kPayloadBytes];
  std::memcpy(future, buf, sizeof(future));
  future[1] = ss::kWireVersion + 1u;
  expect("future wire version rejected", !ss::decode(future, sizeof(future), &out));
}

// ── the merge rule ───────────────────────────────────────────────────────────

void test_higher_revision_wins() {
  const Payload mine = make(4u, 1u, 20u, 1u);
  const Payload theirs = make(5u, 0u, 60u, 0u);
  expect("newer revision applies", ss::should_apply(mine, theirs, true));
  expect("older revision does not", !ss::should_apply(theirs, mine, true));
}

void test_identical_content_is_a_noop() {
  const Payload a = make(7u, 1u, 20u, 1u);
  const Payload b = make(7u, 1u, 20u, 1u);
  expect("equal revision, equal content: no apply", !ss::should_apply(a, b, true));
  expect("and not the other way either", !ss::should_apply(b, a, false));
}

/**
 * The case that decides whether the two ends settle or fight.
 *
 * Both edited at the same revision without seeing each other. Exactly one side
 * must yield, and both must reach that verdict independently.
 */
void test_conflict_resolves_one_way_only() {
  const Payload watch = make(9u, 1u, 20u, 1u);
  const Payload phone = make(9u, 0u, 60u, 0u);

  const bool watch_takes_phones = ss::should_apply(watch, phone, /*self_is_watch=*/true);
  const bool phone_takes_watchs = ss::should_apply(phone, watch, /*self_is_watch=*/false);

  expect("the watch keeps its own on a tie", !watch_takes_phones);
  expect("the phone yields on a tie", phone_takes_watchs);
  expect("exactly one side yields — no ping-pong, no stalemate",
         watch_takes_phones != phone_takes_watchs);
}

/**
 * The Lamport property. Both sides go offline at revision 5, both edit, then
 * they meet. Whoever edited having seen the higher revision must win — a plain
 * per-side sequence number cannot express this.
 */
void test_lamport_counter_makes_last_writer_win() {
  const std::uint32_t shared = 5u;
  // Watch edits first, unaware of anything newer.
  const std::uint32_t watch_rev = ss::next_revision(shared, shared);
  expect("a local edit moves past what was seen", watch_rev == 6u);

  // Phone receives the watch's update, adopts 6, then the user edits on the
  // phone. Its edit must land above the watch's.
  const std::uint32_t phone_rev = ss::next_revision(shared, watch_rev);
  expect("an edit after seeing a remote update outranks it", phone_rev == 7u);

  const Payload from_watch = make(watch_rev, 1u, 20u, 1u);
  const Payload from_phone = make(phone_rev, 0u, 90u, 0u);
  expect("the later edit wins regardless of which device made it",
         ss::should_apply(from_watch, from_phone, true));
}

void test_revision_saturates_rather_than_wrapping() {
  // A wrap would make an ancient revision outrank a current one and silently
  // resurrect old settings.
  expect("saturates at the top", ss::next_revision(0xFFFFFFFFu, 0u) == 0xFFFFFFFFu);
  expect("still saturates from the other side",
         ss::next_revision(0u, 0xFFFFFFFFu) == 0xFFFFFFFFu);
}

void test_apply_leaves_unsynced_fields_alone() {
  slate::local::Settings s{};
  s.tilt_sensitivity = 6u;   // deliberately not synced
  s.magic = 0x1234u;
  ss::apply_to(make(1u, 0u, 90u, 0u), &s);
  expect("synced fields land", s.wake_seconds == 90u && s.tilt_enabled == 0u);
  expect("tilt_sensitivity untouched", s.tilt_sensitivity == 6u);
  expect("magic untouched", s.magic == 0x1234u);
}

void test_from_settings_round_trips() {
  slate::local::Settings s{};
  s.tilt_enabled = 0u;
  s.wake_seconds = 35u;
  s.face_show_steps = 1u;
  s.face_show_diag = 0u;
  s.hr_enabled = 1u;
  s.ui_chrome = 0xF800u;
  s.face_bright = 0x07E0u;
  s.face_dim = 0x001Fu;
  s.raise_sensitivity = 2u;
  s.shake_enabled = 1u;
  s.shake_sensitivity = 0u;
  const Payload p = ss::from(s, 12u);
  expect("from() captures the synced fields",
         p.revision == 12u && p.tilt_enabled == 0u && p.wake_seconds == 35u &&
             p.face_show_steps == 1u && p.face_show_diag == 0u &&
             p.hr_enabled == 1u && p.ui_chrome == 0xF800u &&
             p.face_bright == 0x07E0u && p.face_dim == 0x001Fu &&
             p.raise_sensitivity == 2u && p.shake_enabled == 1u &&
             p.shake_sensitivity == 0u);
}

void test_round_trip_includes_wake_sens() {
  Payload in = make(3u, 1u, 20u, 1u);
  in.raise_sensitivity = 0u;
  in.shake_enabled = 1u;
  in.shake_sensitivity = 2u;
  std::uint8_t buf[32] = {};
  const std::size_t n = ss::encode(in, buf, sizeof(buf));
  expect("v4 length", n == 20u);
  expect("wire version 4", buf[1] == 4u);
  Payload out;
  expect("decodes v4", ss::decode(buf, n, &out));
  expect("raise sens", out.raise_sensitivity == 0u);
  expect("shake on", out.shake_enabled == 1u);
  expect("shake sens", out.shake_sensitivity == 2u);
}

}  // namespace

int main() {
  test_round_trip();
  test_decode_rejects_rubbish();
  test_higher_revision_wins();
  test_identical_content_is_a_noop();
  test_conflict_resolves_one_way_only();
  test_lamport_counter_makes_last_writer_win();
  test_revision_saturates_rather_than_wrapping();
  test_apply_leaves_unsynced_fields_alone();
  test_from_settings_round_trips();
  test_round_trip_includes_wake_sens();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all settings-sync tests passed\n");
  return 0;
}
