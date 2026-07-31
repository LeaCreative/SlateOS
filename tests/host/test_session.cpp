#include "session.hpp"
#include "sdp_opcodes.hpp"
#include "sdp_frame.hpp"
#include "boot_util.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void expect_true(const char* name, bool cond) {
  if (!cond) {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

void expect_eq(const char* name, unsigned got, unsigned want) {
  if (got != want) {
    std::printf("FAIL %s: got %u want %u\n", name, got, want);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

struct Harness {
  std::vector<std::vector<std::uint8_t>> tx;
  std::vector<std::vector<std::uint8_t>> applied;
  int watch_face_calls = 0;
  int stale_calls = 0;
  std::uint8_t last_profile = 0xFF;
  bool reject_lists = false;

  static bool send(std::uint8_t channel, const std::uint8_t* msg, std::size_t len,
                   void* ctx) {
    auto* h = static_cast<Harness*>(ctx);
    std::vector<std::uint8_t> m(msg, msg + len);
    m.insert(m.begin(), channel);  // prefix channel for assertions
    h->tx.push_back(std::move(m));
    return true;
  }

  static slate::session::ApplyListResult apply(const std::uint8_t* data,
                                               std::size_t len, void* ctx) {
    auto* h = static_cast<Harness*>(ctx);
    if (h->reject_lists) {
      return slate::session::ApplyListResult::Reject;
    }
    h->applied.emplace_back(data, data + len);
    return slate::session::ApplyListResult::Ok;
  }

  static void face(bool stale, void* ctx) {
    auto* h = static_cast<Harness*>(ctx);
    ++h->watch_face_calls;
    if (stale) ++h->stale_calls;
  }

  static void profile(const slate::profile::Desc& d, void* ctx) {
    static_cast<Harness*>(ctx)->last_profile = d.id;
  }
};

slate::session::Hooks make_hooks(Harness* h) {
  slate::session::Hooks hk;
  hk.send = &Harness::send;
  hk.apply_list = &Harness::apply;
  hk.show_watch_face = &Harness::face;
  hk.apply_profile = &Harness::profile;
  hk.ctx = h;
  return hk;
}

std::vector<std::uint8_t> hello_accept(std::uint8_t profile,
                                       const std::uint8_t phone[8],
                                       const char* ver) {
  std::vector<std::uint8_t> m;
  m.push_back(sdp::control_op::HELLO_ACCEPT);
  m.push_back(profile);
  m.insert(m.end(), phone, phone + 8);
  const auto len = static_cast<std::uint8_t>(std::strlen(ver));
  m.push_back(len);
  m.insert(m.end(), ver, ver + len);
  return m;
}

bool tx_has_op(const Harness& h, std::uint8_t channel, std::uint8_t op) {
  for (const auto& m : h.tx) {
    if (m.size() >= 2 && m[0] == channel && m[1] == op) return true;
  }
  return false;
}

void test_clean_connect() {
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));

  s.on_link_up(1000);
  expect_eq("clean: Connected", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Connected));
  expect_true("clean: HELLO_OFFER sent",
              tx_has_op(h, sdp::frame::kChanControl, sdp::control_op::HELLO_OFFER));

  const std::uint8_t phone[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  auto acc = hello_accept(slate::profile::kIdActive, phone, "1.0.0");
  s.on_control(acc.data(), acc.size(), 1100);
  expect_eq("clean: Ready", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Ready));
  expect_eq("clean: profile active", s.profile_id(), slate::profile::kIdActive);
  expect_eq("clean: depth 0", s.remote_depth(), 0u);

  const std::uint8_t list[] = {sdp::op::CLEAR, 0, 0, 0, sdp::op::COMMIT, 0};
  s.on_display(list, sizeof(list), 1200);
  expect_eq("clean: Active", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Active));
  expect_eq("clean: depth 1", s.remote_depth(), 1u);
  expect_eq("clean: applied", static_cast<unsigned>(h.applied.size()), 1u);
}

void test_negotiation_failure() {
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));
  s.on_link_up(0);

  // Reject from phone.
  const std::uint8_t rej[] = {sdp::control_op::HELLO_REJECT, 1};
  s.on_control(rej, sizeof(rej), 100);
  expect_eq("nego fail: still Connected", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Connected));

  // Accept with unknown profile id.
  const std::uint8_t phone[8] = {9, 9, 9, 9, 9, 9, 9, 9};
  auto bad = hello_accept(99, phone, "1.0.0");
  s.on_control(bad.data(), bad.size(), 200);
  expect_eq("nego fail: unknown profile", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Connected));

  // DISPLAY ignored before Ready.
  const std::uint8_t list[] = {sdp::op::COMMIT, 0};
  s.on_display(list, sizeof(list), 300);
  expect_eq("nego fail: no apply", static_cast<unsigned>(h.applied.size()), 0u);
}

void test_mid_session_link_loss() {
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));
  s.on_link_up(0);
  const std::uint8_t phone[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  auto acc = hello_accept(slate::profile::kIdActive, phone, "1.0.0");
  s.on_control(acc.data(), acc.size(), 10);
  const std::uint8_t list[] = {sdp::op::COMMIT, 0};
  s.on_display(list, sizeof(list), 20);
  expect_eq("loss: Active before", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Active));

  const int faces_before = h.watch_face_calls;
  s.on_link_down(30);
  expect_eq("loss: Disconnected", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Disconnected));
  expect_eq("loss: depth cleared", s.remote_depth(), 0u);
  expect_true("loss: watch face", h.watch_face_calls > faces_before);
}

void test_reconnect_same_phone() {
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));
  const std::uint8_t phone[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};

  s.on_link_up(0);
  auto acc = hello_accept(slate::profile::kIdAmbient, phone, "1.0.0");
  s.on_control(acc.data(), acc.size(), 10);
  expect_true("same: first not same_phone", !s.same_phone_as_last());
  s.on_link_down(20);

  h.tx.clear();
  s.on_link_up(100);
  expect_eq("same: Connected again", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Connected));
  s.on_control(acc.data(), acc.size(), 110);
  expect_eq("same: Ready", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Ready));
  expect_true("same: same_phone flag", s.same_phone_as_last());
  expect_eq("same: depth fresh", s.remote_depth(), 0u);
  expect_eq("same: ambient profile", s.profile_id(), slate::profile::kIdAmbient);
}

void test_reconnect_different_phone() {
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));
  const std::uint8_t phone_a[8] = {1, 0, 0, 0, 0, 0, 0, 0};
  const std::uint8_t phone_b[8] = {2, 0, 0, 0, 0, 0, 0, 0};

  s.on_link_up(0);
  auto acc_a = hello_accept(slate::profile::kIdActive, phone_a, "1.0.0");
  s.on_control(acc_a.data(), acc_a.size(), 10);
  const std::uint8_t list[] = {sdp::op::COMMIT, 0};
  s.on_display(list, sizeof(list), 20);
  s.on_link_down(30);

  s.on_link_up(100);
  auto acc_b = hello_accept(slate::profile::kIdStreaming, phone_b, "2.0.0");
  s.on_control(acc_b.data(), acc_b.size(), 110);
  expect_eq("diff: Ready", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Ready));
  expect_true("diff: not same phone", !s.same_phone_as_last());
  expect_eq("diff: depth 0", s.remote_depth(), 0u);
  expect_eq("diff: streaming", s.profile_id(), slate::profile::kIdStreaming);
  expect_eq("diff: no leaked screens", static_cast<unsigned>(h.applied.size()), 1u);
}

void test_heartbeat_stale_and_drop() {
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));
  s.on_link_up(0);
  const std::uint8_t phone[8] = {3, 3, 3, 3, 3, 3, 3, 3};
  auto acc = hello_accept(slate::profile::kIdActive, phone, "1.0.0");
  s.on_control(acc.data(), acc.size(), 0);
  const std::uint8_t list[] = {sdp::op::COMMIT, 0};
  s.on_display(list, sizeof(list), 0);

  // 2 missed → stale (4000 ms)
  s.tick(4000);
  expect_true("hb: stale", s.stale());
  expect_eq("hb: Idle", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Idle));

  // Heartbeat clears stale
  const std::uint8_t hb[] = {sdp::control_op::HEARTBEAT};
  s.on_control(hb, sizeof(hb), 4100);
  expect_true("hb: clear stale", !s.stale());
  expect_eq("hb: Active again", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Active));

  // 5 missed from last hb → pop
  s.tick(4100 + 5 * 2000);
  expect_eq("hb: popped to Ready", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Ready));
  expect_eq("hb: depth 0", s.remote_depth(), 0u);
  expect_true("hb: SESSION_END sent",
              tx_has_op(h, sdp::frame::kChanInput, sdp::input_op::SESSION_END));
}

void test_heartbeat_across_mono_ms_wrap() {
  // slate::time::mono_ms wraps at 2^32. Across that boundary unsigned elapsed
  // must stay small — unlike the old micros()/1000 clock, which jumped from
  // ~4,294,967 → 0 and made (500 - 4,294,000) look like ~4.29e9 ms (spurious
  // TIMEOUT pop). Drive the same *shape* of discontinuity the field hit, then
  // the true u32 wrap.
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));
  s.on_link_up(0);
  const std::uint8_t phone[8] = {5, 5, 5, 5, 5, 5, 5, 5};
  auto acc = hello_accept(slate::profile::kIdActive, phone, "1.0.0");
  s.on_control(acc.data(), acc.size(), 0);
  const std::uint8_t list[] = {sdp::op::COMMIT, 0};
  s.on_display(list, sizeof(list), 0);

  // Former micros()/1000 wrap neighbourhood: last_hb near 4.294e6, now=500.
  // With a correct 2^32-wrapping clock those are not consecutive samples; if a
  // buggy caller still fed them, unsigned elapsed is huge. After mono_ms the
  // production path never emits that jump — this documents the hazard and
  // locks the *real* wrap test below.
  constexpr std::uint32_t kOldWrapLast = 4294000u;
  constexpr std::uint32_t kOldWrapNow = 500u;
  const std::uint8_t hb[] = {sdp::control_op::HEARTBEAT};
  s.on_control(hb, sizeof(hb), kOldWrapLast);
  // True wrap: last just before UINT32_MAX, now just after.
  constexpr std::uint32_t kLast = 0xFFFFF000u;
  s.on_control(hb, sizeof(hb), kLast);
  // +1500 ms → < 2 heartbeat periods → must not stale or pop.
  s.tick(kLast + 1500u);
  expect_true("wrap: not stale after 1.5s", !s.stale());
  expect_eq("wrap: still Active", static_cast<unsigned>(s.state()),
            static_cast<unsigned>(slate::session::State::Active));
  expect_eq("wrap: depth kept", s.remote_depth(), 1u);

  // Cross UINT32_MAX: now = kLast + 1500 overflows.
  const std::uint32_t now_wrapped =
      static_cast<std::uint32_t>(kLast + 1500u);  // wraps
  (void)kOldWrapNow;
  (void)now_wrapped;
  const std::uint32_t across =
      static_cast<std::uint32_t>(0x1000u);  // 0xFFFFF000 + 0x2000 wraps to 0x1000
  s.on_control(hb, sizeof(hb), kLast);
  s.tick(across);  // elapsed = 0x1000 - 0xFFFFF000 = 0x2000 = 8192 ms → 4 misses
  // 4 misses → stale but not drop (drop at 5).
  expect_true("wrap: stale at 4 misses across u32", s.stale());
  expect_eq("wrap: depth still 1", s.remote_depth(), 1u);
  expect_true("wrap: no TIMEOUT pop yet",
              !tx_has_op(h, sdp::frame::kChanInput, sdp::input_op::SESSION_END));
}

void test_confirm_dwell_across_u32_wrap() {
  // Mirror tick_confirm's dwell math (boot::dwell_satisfied).
  constexpr std::uint32_t kSince = 0xFFFFF800u;
  expect_true(
      "dwell: not yet",
      !slate::boot::dwell_satisfied(kSince + 1000u, kSince));
  const std::uint32_t after_wrap =
      static_cast<std::uint32_t>(kSince + slate::boot::kConfirmDwellMs);
  expect_true("dwell: met across wrap",
              slate::boot::dwell_satisfied(after_wrap, kSince));
  // Old micros()/1000 discontinuity must not be treated as "dwell met" if
  // someone naively compared absolute values — dwell_satisfied uses unsigned
  // elapsed only. (500 - 4294000) is huge, so it *would* look satisfied; the
  // fix is that mono_ms never produces that jump. Guard the helper still uses
  // modular elapsed for the real wrap.
  expect_true(
      "dwell: old-wrap pair is huge elapsed (why mono_ms must not emit it)",
      slate::boot::dwell_satisfied(500u, 4294000u));
}

void test_profile_select_not_invent() {
  Harness h;
  slate::session::Manager s;
  s.init(make_hooks(&h));
  s.on_link_up(0);
  const std::uint8_t phone[8] = {4, 4, 4, 4, 4, 4, 4, 4};
  auto acc = hello_accept(slate::profile::kIdActive, phone, "1.0.0");
  s.on_control(acc.data(), acc.size(), 0);

  const std::uint8_t bad[] = {sdp::control_op::SET_PROFILE, 42};
  s.on_control(bad, sizeof(bad), 10);
  expect_true("prof: NACK",
              tx_has_op(h, sdp::frame::kChanControl, sdp::control_op::PROFILE_ACK));
  expect_eq("prof: still active", s.profile_id(), slate::profile::kIdActive);

  const std::uint8_t ok[] = {sdp::control_op::SET_PROFILE, slate::profile::kIdAmbient};
  s.on_control(ok, sizeof(ok), 20);
  expect_eq("prof: ambient", s.profile_id(), slate::profile::kIdAmbient);
}

void test_hello_offer_encodes_profiles() {
  std::uint8_t buf[160];
  const std::size_t n =
      slate::session::Manager::encode_hello_offer(buf, sizeof(buf), true);
  expect_true("offer: non-empty", n > 40u);
  expect_eq("offer: op", buf[0], sdp::control_op::HELLO_OFFER);
  // Find profile_count after bitmap
  // op(1)+fw(3)+proto(2)+w(2)+h(2)+bitmap(32)+free(2) = 44; count at 44
  expect_eq("offer: profile_count", buf[44], slate::profile::kCatalogCount);
}

// N-6: byte-exact golden, built independently from the wire constants and the
// profile catalog. The same bytes pin the Kotlin fixture/decoder
// (SessionTestFixtures.encodeHelloOffer in sdp-tests).
void test_hello_offer_golden() {
  std::vector<std::uint8_t> want;
  auto w8 = [&](std::uint8_t b) { want.push_back(b); };
  auto w16 = [&](std::uint16_t v) {
    w8(static_cast<std::uint8_t>(v & 0xFFu));
    w8(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  };
  w8(sdp::control_op::HELLO_OFFER);
  w8(sdp::kFwMajor);
  w8(sdp::kFwMinor);
  w8(sdp::kFwPatch);
  w16(sdp::kProtocolVersion);
  w16(static_cast<std::uint16_t>(sdp::kDisplaySize));
  w16(static_cast<std::uint16_t>(sdp::kDisplaySize));
  std::uint8_t bitmap[sdp::kOpcodeBitmapBytes];
  slate::session::Manager::fill_opcode_bitmap(bitmap);
  want.insert(want.end(), bitmap, bitmap + sizeof(bitmap));
  w16(static_cast<std::uint16_t>(sdp::kMaxListBytes));
  w8(slate::profile::kCatalogCount);
  for (std::uint8_t i = 0u; i < slate::profile::kCatalogCount; ++i) {
    const auto& d = slate::profile::kCatalog[i];
    w8(d.id);
    const std::size_t namelen = std::strlen(d.name);
    w8(static_cast<std::uint8_t>(namelen));
    for (std::size_t c = 0u; c < namelen; ++c) {
      w8(static_cast<std::uint8_t>(d.name[c]));
    }
    w8(d.backlight);
    w16(d.interval_units);
  }
  w8(0u);     // asset_pack_count
  w8(0x01u);  // flags: diag available

  std::uint8_t buf[slate::session::kHelloOfferBufBytes];
  const std::size_t n =
      slate::session::Manager::encode_hello_offer(buf, sizeof(buf), true);
  expect_eq("golden: length", static_cast<unsigned>(n),
            static_cast<unsigned>(want.size()));
  expect_true("golden: bytes",
              n == want.size() && std::memcmp(buf, want.data(), n) == 0);
  expect_true("golden: within worst-case bound",
              n <= slate::session::kHelloOfferMaxBytes);

  std::printf("  hello_offer golden (%zu B): ", n);
  for (std::size_t i = 0u; i < n; ++i) {
    std::printf("%02X", buf[i]);
  }
  std::printf("\n");
}

// N-6: overflow must yield 0, never a truncated offer.
void test_hello_offer_overflow_returns_zero() {
  std::uint8_t buf[160];
  const std::size_t full =
      slate::session::Manager::encode_hello_offer(buf, sizeof(buf), false);
  expect_true("overflow: full encode ok", full > 0u);
  expect_eq("overflow: cap-1 rejected",
            static_cast<unsigned>(slate::session::Manager::encode_hello_offer(
                buf, full - 1u, false)),
            0u);
  expect_eq("overflow: cap 0 rejected",
            static_cast<unsigned>(
                slate::session::Manager::encode_hello_offer(buf, 0u, false)),
            0u);
  expect_eq("overflow: null out rejected",
            static_cast<unsigned>(slate::session::Manager::encode_hello_offer(
                nullptr, 160u, false)),
            0u);
}

}  // namespace

int main() {
  test_clean_connect();
  test_negotiation_failure();
  test_mid_session_link_loss();
  test_reconnect_same_phone();
  test_reconnect_different_phone();
  test_heartbeat_stale_and_drop();
  test_heartbeat_across_mono_ms_wrap();
  test_confirm_dwell_across_u32_wrap();
  test_profile_select_not_invent();
  test_hello_offer_encodes_profiles();
  test_hello_offer_golden();
  test_hello_offer_overflow_returns_zero();
  if (g_failures != 0) {
    std::printf("%d FAILURES\n", g_failures);
    return 1;
  }
  std::printf("all session tests passed\n");
  return 0;
}
