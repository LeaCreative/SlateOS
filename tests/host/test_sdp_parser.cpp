#include "sdp_parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void expect_status(const char* name, sdp::Status got, sdp::Status want) {
  if (got != want) {
    std::printf("FAIL %s: status got %u want %u\n", name,
                static_cast<unsigned>(got), static_cast<unsigned>(want));
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

void expect_true(const char* name, bool cond) {
  if (!cond) {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

sdp::ParseResult run(const std::vector<std::uint8_t>& bytes) {
  sdp::ParseOptions opt;
  opt.execute = false;
  return sdp::parse(bytes.data(), bytes.size(), opt, nullptr);
}

void push_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
  v.push_back(static_cast<std::uint8_t>(x & 0xFFu));
  v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFFu));
}

void color_literal(std::vector<std::uint8_t>& v, std::uint16_t rgb) {
  v.push_back(0x00);
  push_u16(v, rgb);
}

void color_pal(std::vector<std::uint8_t>& v, std::uint8_t idx1based) {
  v.push_back(idx1based);  // 0x01..0x10
}

// ── Happy paths: every opcode ────────────────────────────────────────────────

void test_clear_commit() {
  std::vector<std::uint8_t> b = {0x01};
  color_literal(b, 0x0000);
  b.push_back(0xF0);
  b.push_back(0x00);
  auto r = run(b);
  expect_status("CLEAR+COMMIT", r.status, sdp::Status::Ok);
  expect_true("saw commit", r.saw_commit);
}

void test_set_palette_rect() {
  std::vector<std::uint8_t> b = {0x02, 0x00};
  push_u16(b, 0xF800);
  b.insert(b.end(), {0x03, 10, 10, 20, 20});
  color_pal(b, 0x01);
  b.push_back(0x00);  // STYLE fill
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("SET_PALETTE+RECT", run(b).status, sdp::Status::Ok);
}

void test_all_shape_ops() {
  std::vector<std::uint8_t> b;
  // RECT_ROUND
  b.insert(b.end(), {0x04, 5, 5, 30, 30, 4});
  color_literal(b, 0x07E0);
  b.push_back(0x00);
  // LINE
  b.insert(b.end(), {0x05, 0, 0, 10, 10});
  color_literal(b, 0x001F);
  b.push_back(1);
  // CIRCLE
  b.insert(b.end(), {0x06, 120, 120, 20});
  color_literal(b, 0xFFFF);
  b.push_back(0x01);  // stroke
  // ARC
  b.insert(b.end(), {0x07, 120, 120, 30});
  push_u16(b, 0);
  push_u16(b, 90);
  color_literal(b, 0xFFE0);
  b.push_back(2);
  // POLYLINE 3 pts
  b.push_back(0x08);
  b.push_back(3);
  color_literal(b, 0xF81F);
  b.push_back(1);
  b.insert(b.end(), {10, 10, 20, 20, 30, 10});
  // CLIP
  b.insert(b.end(), {0x09, 0, 0, 240, 240});  // wait 240 is invalid for w if x=0 — w max 240 means x+w<=240 so w=240 ok with x=0... check_rect: x+w > 240 rejects. 0+240=240 which is NOT > 240, so OK.
  b.push_back(0x0A);
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("shapes+clip", run(b).status, sdp::Status::Ok);
}

void test_text_and_box() {
  std::vector<std::uint8_t> b;
  b.insert(b.end(), {0x10, 0, 10, 10});
  color_literal(b, 0xFFFF);
  b.push_back(0);  // LEFT
  b.push_back(3);
  b.insert(b.end(), {'1', '2', '3'});
  b.insert(b.end(), {0x11, 0, 10, 20, 100, 40});
  color_literal(b, 0xFFFF);
  b.push_back(0);
  b.push_back(0x01);  // WRAP
  b.push_back(2);
  b.insert(b.end(), {'H', 'i'});
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("TEXT+TEXT_BOX", run(b).status, sdp::Status::Ok);
}

void test_icon_image() {
  std::vector<std::uint8_t> b;
  b.insert(b.end(), {0x12, 0});
  push_u16(b, 0);
  b.insert(b.end(), {8, 8});
  color_literal(b, 0x07FF);
  b.insert(b.end(), {0x13, 0});
  push_u16(b, 0);
  b.insert(b.end(), {16, 16});
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("ICON+IMAGE", run(b).status, sdp::Status::Ok);
}

void test_progress_elem_scroll() {
  std::vector<std::uint8_t> b;
  b.insert(b.end(), {0x20, 10, 200, 100, 10, 50});
  color_literal(b, 0xF800);
  color_literal(b, 0x2104);
  b.insert(b.end(), {0x21, 120, 120, 40, 75});
  color_literal(b, 0x07E0);
  color_literal(b, 0x0000);
  b.push_back(3);
  b.push_back(0x30);
  push_u16(b, 42);
  b.insert(b.end(), {20, 180, 60, 40, 0x00});
  b.push_back(0x31);
  b.insert(b.end(), {0x40, 40, 100});
  push_u16(b, 300);
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("progress+elem+scroll", run(b).status, sdp::Status::Ok);
}

void test_patch_haptic_backlight_retain() {
  std::vector<std::uint8_t> b;
  b.insert(b.end(), {0x50, 0, 0, 0, 1, 1, 0, 0});  // RGB565 RAW
  push_u16(b, 2);
  b.insert(b.end(), {0x00, 0xF8});  // one red pixel LE
  b.insert(b.end(), {0x51, 0, 0, 0});
  b.insert(b.end(), {0x60, 0});
  b.insert(b.end(), {0x61, 50});
  b.push_back(0xF1);
  push_u16(b, 60);
  b.push_back(0xF0);
  b.push_back(0x00);
  auto r = run(b);
  expect_status("patch+haptic+bl+retain", r.status, sdp::Status::Ok);
  expect_true("retain ttl", r.saw_retain && r.retain_ttl_s == 60);
}

void test_ext_skip() {
  std::vector<std::uint8_t> b = {0xE5, 0x04, 0x00, 1, 2, 3, 4, 0xF0, 0x00};
  expect_status("EXT skip unknown", run(b).status, sdp::Status::Ok);
}

// ── Malformed ────────────────────────────────────────────────────────────────

void test_trunc_color() {
  std::vector<std::uint8_t> b = {0x01, 0x00};  // missing rgb565
  expect_status("trunc COLOR", run(b).status, sdp::Status::Truncated);
}

void test_bad_coord() {
  std::vector<std::uint8_t> b = {0x03, 240, 0, 1, 1};
  color_literal(b, 0);
  b.push_back(0x00);
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("coord 240", run(b).status, sdp::Status::Reject);
}

void test_style_mode3() {
  std::vector<std::uint8_t> b = {0x03, 0, 0, 10, 10};
  color_literal(b, 0);
  b.push_back(0x03);  // mode 3
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("STYLE mode 3", run(b).status, sdp::Status::Reject);
}

void test_color_tag_reserved() {
  std::vector<std::uint8_t> b = {0x01, 0x11, 0xF0, 0x00};
  expect_status("COLOR tag 0x11", run(b).status, sdp::Status::Reject);
}

void test_bad_align() {
  std::vector<std::uint8_t> b = {0x10, 0, 0, 0};
  color_literal(b, 0);
  b.push_back(3);  // bad align
  b.push_back(0);
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("bad align", run(b).status, sdp::Status::Reject);
}

void test_oversized_string() {
  std::vector<std::uint8_t> b = {0x10, 0, 0, 0};
  color_literal(b, 0);
  b.push_back(0);
  b.push_back(10);  // claims 10 bytes
  b.insert(b.end(), {'a', 'b', 'c'});  // only 3
  expect_status("oversized string", run(b).status, sdp::Status::Truncated);
}

void test_op_count_overflow() {
  std::vector<std::uint8_t> b;
  for (int i = 0; i < 513; ++i) {
    b.push_back(0x0A);  // CLIP_CLEAR — 0 operands
  }
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("513 ops", run(b).status, sdp::Status::Reject);
}

void test_list_too_big() {
  std::vector<std::uint8_t> b(4097, 0x0A);
  expect_status(">4KB", run(b).status, sdp::Status::Reject);
}

void test_ext_bad_length() {
  std::vector<std::uint8_t> b = {0xE0, 0x10, 0x00, 0x01};  // claims 16, has 1
  expect_status("EXT past EOF", run(b).status, sdp::Status::Truncated);
}

void test_end_without_begin() {
  std::vector<std::uint8_t> b = {0x31, 0xF0, 0x00};
  expect_status("END without BEGIN", run(b).status, sdp::Status::Reject);
}

void test_unset_palette() {
  std::vector<std::uint8_t> b = {0x01, 0x01};  // palette 0 unset
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("unset palette", run(b).status, sdp::Status::Reject);
}

void test_bad_font_id() {
  std::vector<std::uint8_t> b = {0x10, 1, 0, 0};  // font 1 not registered
  color_literal(b, 0);
  b.push_back(0);
  b.push_back(0);
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("bad font id", run(b).status, sdp::Status::Reject);
}

void test_commit_with_open_elem() {
  std::vector<std::uint8_t> b = {0x30};
  push_u16(b, 1);
  b.insert(b.end(), {0, 0, 10, 10, 0});
  b.push_back(0xF0);
  b.push_back(0x00);
  expect_status("COMMIT with open elem", run(b).status, sdp::Status::Reject);
}

}  // namespace

int main() {
  test_clear_commit();
  test_set_palette_rect();
  test_all_shape_ops();
  test_text_and_box();
  test_icon_image();
  test_progress_elem_scroll();
  test_patch_haptic_backlight_retain();
  test_ext_skip();

  test_trunc_color();
  test_bad_coord();
  test_style_mode3();
  test_color_tag_reserved();
  test_bad_align();
  test_oversized_string();
  test_op_count_overflow();
  test_list_too_big();
  test_ext_bad_length();
  test_end_without_begin();
  test_unset_palette();
  test_bad_font_id();
  test_commit_with_open_elem();

  if (g_failures != 0) {
    std::printf("%d failure(s)\n", g_failures);
    return EXIT_FAILURE;
  }
  std::printf("all sdp parser checks passed\n");
  return EXIT_SUCCESS;
}
