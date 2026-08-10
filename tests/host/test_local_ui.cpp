#include "local_state.hpp"
#include "local_ui.hpp"
#include "notif_store.hpp"
#include "persist.hpp"
#include "sdp_opcodes.hpp"
#include "sdp_parser.hpp"
#include "wall_clock.hpp"

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

// Persist stubs so wall_clock (pulled in via build_face) links.
std::size_t host_read(slate::persist::Slot, void*, std::size_t, void*) {
  return 0u;
}
bool host_write(slate::persist::Slot, const void*, std::size_t, void*) {
  return true;
}
std::uint64_t host_ticks(void*) { return 0u; }

// Count TEXT (0x10) vs TEXT_SCALED (0xE0) by walking opcodes the same way the
// firmware skip/validate path does for length-prefixed extensions.
void count_text_ops(const std::uint8_t* p, std::size_t n, int* text,
                    int* scaled) {
  *text = 0;
  *scaled = 0;
  std::size_t i = 0u;
  while (i < n) {
    const std::uint8_t op = p[i++];
    if (op == sdp::op::COMMIT || op == sdp::op::RETAIN) {
      if (i < n) {
        ++i;  // flags
      }
      break;
    }
    if (op >= sdp::op::EXT_MIN && op <= sdp::op::EXT_MAX) {
      if (i + 2u > n) {
        break;
      }
      const std::uint16_t plen =
          static_cast<std::uint16_t>(p[i]) |
          (static_cast<std::uint16_t>(p[i + 1]) << 8);
      i += 2u;
      if (op == sdp::op::TEXT_SCALED) {
        ++(*scaled);
      }
      if (i + plen > n) {
        break;
      }
      i += plen;
      continue;
    }
    if (op == sdp::op::TEXT) {
      ++(*text);
      // font x y color align len + chars
      if (i + 6u > n) {
        break;
      }
      const std::uint8_t len = p[i + 5];
      i += 6u + len;
      continue;
    }
    // Coarse skip for other base ops used by local builders.
    switch (op) {
      case sdp::op::CLEAR:
        i += 3u;  // COLOR
        break;
      case sdp::op::SET_PALETTE:
        i += 3u;  // idx + rgb565
        break;
      case sdp::op::RECT:
        i += 4u + 3u + 1u;  // xywh + COLOR + STYLE
        break;
      case sdp::op::RECT_ROUND:
        i += 5u + 3u + 1u;  // xywh rad + COLOR + STYLE
        break;
      case sdp::op::PROGRESS_BAR:
        i += 4u + 1u + 3u + 3u;  // xywh pct COLOR COLOR
        break;
      case sdp::op::SCROLL_REGION:
        i += 1u + 1u + 2u;
        break;
      case sdp::op::BEGIN_ELEM:
        i += 2u + 4u + 1u;  // id xywh flags
        break;
      case sdp::op::END_ELEM:
        break;
      default:
        // Unknown — abort walk.
        i = n;
        break;
    }
  }
}

bool parse_ok(const std::uint8_t* p, std::size_t n) {
  sdp::ParseOptions opt;
  opt.execute = false;
  return sdp::parse(p, n, opt, nullptr).status == sdp::Status::Ok;
}

void fill_notifs(slate::notif::Store* store, std::uint8_t n, bool stale) {
  slate::notif::init(store);
  store->count = n;
  for (std::uint8_t i = 0u; i < n; ++i) {
    auto& e = store->entries[i];
    e.monogram = static_cast<char>('0' + (i % 10u));
    e.title_len = 32u;  // worst-case digit width cue
    e.stale = stale ? 1u : 0u;
  }
}

}  // namespace

static void test_disconnected_golden() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Disconnected;
  slate::ui::ViewModel vm{&st, nullptr, nullptr};
  std::uint8_t buf[512];
  const std::size_t n = slate::ui::build_screen(vm, buf, sizeof(buf));

  // This screen used to be a byte-for-byte golden of a single "0" at scale 8
  // — all the built-in font could say, having no letters. It now carries the
  // real prompt, so assert the properties that matter rather than pinning
  // every byte of four text runs: it must parse, it must use the 5x7 (font 0
  // cannot render this text), and it must actually say something.
  expect("disconnected non-empty", n > 0u && n <= sizeof(buf));
  expect("disconnected parse", parse_ok(buf, n));

  int text = 0, scaled = 0;
  count_text_ops(buf, n, &text, &scaled);
  expect("disconnected uses TEXT_SCALED x4", scaled == 4);
  expect("disconnected no unscaled TEXT", text == 0);

  // Every text run must name font 1: on font 0 this screen is a row of boxes,
  // which is precisely the bug it exists to report. TEXT_SCALED is
  // [op][len:u16][font], so the font byte sits 3 past the opcode.
  bool all_font1 = true;
  for (std::size_t i = 0u; i + 3u < n; ++i) {
    if (buf[i] == sdp::op::TEXT_SCALED && buf[i + 3] != 1u) {
      all_font1 = false;
    }
  }
  expect("disconnected text is font 1 (5x7)", all_font1);
}

static void test_alert_scaled() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Alert;
  st.alert_kind = 1u;
  st.alert_id = 2u;
  slate::ui::ViewModel vm{&st, nullptr, nullptr};
  std::uint8_t buf[512];
  const std::size_t n = slate::ui::build_screen(vm, buf, sizeof(buf));
  int text = 0, scaled = 0;
  count_text_ops(buf, n, &text, &scaled);
  expect("alert non-empty", n > 0u && n <= 512u);
  expect("alert no TEXT", text == 0);
  expect("alert TEXT_SCALED x2", scaled == 2);
  expect("alert parse", parse_ok(buf, n));
}

/**
 * The settings screen must be *tappable*, not merely non-empty.
 *
 * The old assertion counted four TEXT_SCALED ops, which the previous screen
 * satisfied while being a title and three unlabelled numbers with no tap
 * targets at all. Counting text says nothing about whether the screen works.
 * What matters is that every row carries an element carrying EMIT_TOUCH, with
 * the id Core::on_tap_elem switches on — if those drift apart the row silently
 * stops responding and nothing else notices.
 */
static void test_settings_rows_are_tappable() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Settings;
  st.settings.tilt_enabled = 1u;
  st.settings.wake_seconds = 20u;
  st.settings.face_show_steps = 1u;
  st.settings.face_show_diag = 1u;
  slate::ui::ViewModel vm{&st, nullptr, nullptr};
  std::uint8_t buf[512];
  const std::size_t n = slate::ui::build_screen(vm, buf, sizeof(buf));
  expect("settings non-empty", n > 0u && n <= 512u);
  expect("settings parse", parse_ok(buf, n));

  int text = 0, scaled = 0;
  count_text_ops(buf, n, &text, &scaled);
  // Title plus a label and a value for each of the five rows.
  expect("settings TEXT_SCALED x11", scaled == 11);
  expect("settings no TEXT", text == 0);

  // Walk the list for BEGIN_ELEM (0x30): u16 id, x, y, w, h, flags.
  bool saw_raise = false, saw_timeout = false, saw_steps = false,
       saw_diag = false, saw_hr = false;
  int touchable = 0;
  int rounds = 0;
  for (std::size_t i = 0u; i + 8u <= n; ++i) {
    if (buf[i] == sdp::op::RECT_ROUND) {
      ++rounds;
    }
    if (buf[i] != sdp::op::BEGIN_ELEM) {
      continue;
    }
    const std::uint16_t id =
        static_cast<std::uint16_t>(buf[i + 1u] | (buf[i + 2u] << 8));
    const std::uint8_t flags = buf[i + 7u];
    if ((flags & sdp::elem_flags::EMIT_TOUCH) == 0u) {
      continue;
    }
    ++touchable;
    if (id == slate::local::kSettingRaise) saw_raise = true;
    if (id == slate::local::kSettingTimeout) saw_timeout = true;
    if (id == slate::local::kSettingSteps) saw_steps = true;
    if (id == slate::local::kSettingDiag) saw_diag = true;
    if (id == slate::local::kSettingHr) saw_hr = true;
  }
  expect("five touchable rows", touchable == 5);
  expect("raise row carries its id", saw_raise);
  expect("timeout row carries its id", saw_timeout);
  expect("steps row carries its id", saw_steps);
  expect("diag row carries its id", saw_diag);
  expect("hr row carries its id", saw_hr);
  // Edge + fill per row (byte-scan can also catch RECT_ROUND in payloads).
  expect("at least ten rounded rects for button chrome", rounds >= 10);
}

/** A timeout of 0 reads as "Never", not as the number zero or "Off". */
static void test_settings_timeout_off_reads_as_off() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Settings;
  st.settings.wake_seconds = 0u;
  slate::ui::ViewModel vm{&st, nullptr, nullptr};
  std::uint8_t buf[512];
  const std::size_t n = slate::ui::build_screen(vm, buf, sizeof(buf));
  expect("timeout=0 screen parses", n > 0u && parse_ok(buf, n));
  bool found_never = false;
  for (std::size_t i = 0u; i + 5u <= n; ++i) {
    if (buf[i] == 'N' && buf[i + 1u] == 'e' && buf[i + 2u] == 'v' &&
        buf[i + 3u] == 'e' && buf[i + 4u] == 'r') {
      found_never = true;
      break;
    }
  }
  expect("timeout 0 renders as Never", found_never);
}

static void test_notifs_budget() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Notifs;
  slate::notif::Store store{};
  fill_notifs(&store, 6u, true);
  slate::ui::ViewModel vm{&st, &store, nullptr};
  std::uint8_t buf[512];
  const std::size_t n = slate::ui::build_screen(vm, buf, sizeof(buf));
  int text = 0, scaled = 0;
  count_text_ops(buf, n, &text, &scaled);
  std::printf("  notifs 6-stale bytes=%zu scaled=%d\n", n, scaled);
  expect("notifs fits 512", n > 0u && n <= 512u);
  expect("notifs no TEXT", text == 0);
  // header + count + 6*(mono+title+stale) = 2 + 18 = 20
  expect("notifs TEXT_SCALED count", scaled == 20);
  expect("notifs parse", parse_ok(buf, n));
}

static void test_notifs_empty() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Notifs;
  slate::notif::Store store{};
  slate::notif::init(&store);
  slate::ui::ViewModel vm{&st, &store, nullptr};
  std::uint8_t buf[512];
  const std::size_t n = slate::ui::build_screen(vm, buf, sizeof(buf));
  int text = 0, scaled = 0;
  count_text_ops(buf, n, &text, &scaled);
  expect("notifs empty ok", n > 0u && n <= 512u);
  expect("notifs empty no TEXT", text == 0);
  expect("notifs empty scaled x2", scaled == 2);
  expect("notifs empty parse", parse_ok(buf, n));
}

int main() {
  slate::persist::Hooks ph{&host_read, &host_write, nullptr};
  slate::persist::init(ph);
  slate::clock::Hooks ch{&host_ticks, nullptr};
  slate::clock::init(ch);

  test_disconnected_golden();
  test_alert_scaled();
  test_settings_rows_are_tappable();
  test_settings_timeout_off_reads_as_off();
  test_notifs_empty();
  test_notifs_budget();

  if (g_fails != 0) {
    std::printf("%d failures\n", g_fails);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
