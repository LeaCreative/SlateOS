#include "local_state.hpp"
#include "local_ui.hpp"
#include "notif_store.hpp"
#include "persist.hpp"
#include "sdp_opcodes.hpp"
#include "sdp_parser.hpp"
#include "slate_version.hpp"
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
    e.monogram = static_cast<char>('A' + (i % 26u));
    const char* name = "AppNameXXXXXXXX";  // 16 chars — UI truncates
    e.title_len = 16u;
    std::memcpy(e.title, name, 16u);
    e.stale = stale ? 1u : 0u;
  }
}

bool list_contains(const std::uint8_t* p, std::size_t n, const char* needle) {
  const std::size_t nl = std::strlen(needle);
  if (p == nullptr || needle == nullptr || nl == 0u || n < nl) {
    return false;
  }
  for (std::size_t i = 0u; i + nl <= n; ++i) {
    if (std::memcmp(p + i, needle, nl) == 0) {
      return true;
    }
  }
  return false;
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
  // Title plus a label and a value for each of the four visible rows.
  expect("settings TEXT_SCALED x9", scaled == 9);
  expect("settings no TEXT", text == 0);

  // Walk the list for BEGIN_ELEM (0x30): u16 id, x, y, w, h, flags.
  bool saw_raise = false, saw_raise_sens = false, saw_shake = false,
       saw_shake_sens = false;
  int touchable = 0;
  int rounds = 0;
  std::uint8_t ys[8] = {};
  for (std::size_t i = 0u; i + 8u <= n; ++i) {
    if (buf[i] == sdp::op::RECT_ROUND) {
      ++rounds;
    }
    if (buf[i] != sdp::op::BEGIN_ELEM) {
      continue;
    }
    const std::uint16_t id =
        static_cast<std::uint16_t>(buf[i + 1u] | (buf[i + 2u] << 8));
    const std::uint8_t y = buf[i + 4u];
    const std::uint8_t flags = buf[i + 7u];
    if ((flags & sdp::elem_flags::EMIT_TOUCH) == 0u) {
      continue;
    }
    if (touchable < 8) {
      ys[touchable] = y;
    }
    ++touchable;
    if (id == slate::local::kSettingRaise) saw_raise = true;
    if (id == slate::local::kSettingRaiseSens) saw_raise_sens = true;
    if (id == slate::local::kSettingShake) saw_shake = true;
    if (id == slate::local::kSettingShakeSens) saw_shake_sens = true;
  }
  expect("four touchable rows on page 0", touchable == 4);
  expect("raise row carries its id", saw_raise);
  expect("raise sens row carries its id", saw_raise_sens);
  expect("shake row carries its id", saw_shake);
  expect("shake sens row carries its id", saw_shake_sens);
  for (int i = 1; i < touchable; ++i) {
    expect("settings row Y increases", ys[i] > ys[i - 1]);
  }
  expect("at least four rounded rects for button chrome", rounds >= 4);

  // Page 2 shows timeout / steps / diag / HR.
  st.settings_sel = slate::local::kSettingsPageRows;
  const std::size_t n2 = slate::ui::build_screen(vm, buf, sizeof(buf));
  expect("settings page 2 non-empty", n2 > 0u && parse_ok(buf, n2));
  bool saw_timeout = false, saw_hr = false;
  int touchable2 = 0;
  for (std::size_t i = 0u; i + 8u <= n2; ++i) {
    if (buf[i] != sdp::op::BEGIN_ELEM) {
      continue;
    }
    const std::uint16_t id =
        static_cast<std::uint16_t>(buf[i + 1u] | (buf[i + 2u] << 8));
    const std::uint8_t flags = buf[i + 7u];
    if ((flags & sdp::elem_flags::EMIT_TOUCH) == 0u) {
      continue;
    }
    ++touchable2;
    if (id == slate::local::kSettingTimeout) saw_timeout = true;
    if (id == slate::local::kSettingHr) saw_hr = true;
  }
  expect("four touchable rows on page 1", touchable2 == 4);
  expect("timeout on page 1", saw_timeout);
  expect("hr on page 1", saw_hr);
}

/** A timeout of 0 reads as "Never", not as the number zero or "Off". */
static void test_settings_timeout_off_reads_as_off() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Settings;
  st.settings.wake_seconds = 0u;
  st.settings_sel = slate::local::kSettingsPageRows;  // Timeout is on page 1
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
  fill_notifs(&store, 4u, true);
  slate::ui::ViewModel vm{&st, &store, nullptr};
  std::uint8_t buf[512];
  const std::size_t n = slate::ui::build_screen(vm, buf, sizeof(buf));
  int text = 0, scaled = 0;
  count_text_ops(buf, n, &text, &scaled);
  std::printf("  notifs 6-stale bytes=%zu scaled=%d\n", n, scaled);
  expect("notifs fits 512", n > 0u && n <= 512u);
  expect("notifs no TEXT", text == 0);
  // header + up to 4 labels (+ optional arrows with more than 4 stubs)
  expect("notifs TEXT_SCALED at least header+rows", scaled >= 5);
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

static void test_face_version_follows_diag() {
  slate::local::State st{};
  st.screen = slate::local::Screen::Face;
  st.settings.face_show_diag = 1u;
  slate::ui::ViewModel vm{&st, nullptr, nullptr};
  std::uint8_t buf[2048];
  const std::size_t n_on = slate::ui::build_screen(vm, buf, sizeof(buf));
  expect("face with diag non-empty", n_on > 0u && n_on <= sizeof(buf));
  expect("face with diag parse", parse_ok(buf, n_on));
  expect("face with diag includes version",
         list_contains(buf, n_on, slate::version::kVersion));

  st.settings.face_show_diag = 0u;
  const std::size_t n_off = slate::ui::build_screen(vm, buf, sizeof(buf));
  expect("face without diag non-empty", n_off > 0u && n_off <= sizeof(buf));
  expect("face without diag parse", parse_ok(buf, n_off));
  expect("face without diag omits version",
         !list_contains(buf, n_off, slate::version::kVersion));
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
  test_face_version_follows_diag();

  if (g_fails != 0) {
    std::printf("%d failures\n", g_fails);
    return 1;
  }
  std::printf("all passed\n");
  return 0;
}
