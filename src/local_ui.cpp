#include "local_ui.hpp"

#include "sdp_opcodes.hpp"
#include "wall_clock.hpp"

#include <cstring>

namespace slate {
namespace ui {
namespace {

struct W {
  std::uint8_t* p = nullptr;
  std::size_t n = 0u;
  std::size_t cap = 0u;
  bool ok = true;

  void b(std::uint8_t v) {
    if (n >= cap) {
      ok = false;
      return;
    }
    p[n++] = v;
  }
  void u16(std::uint16_t v) {
    b(static_cast<std::uint8_t>(v & 0xFFu));
    b(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
  }
  void rgb(std::uint16_t c) {
    b(0x00u);  // literal RGB565
    u16(c);
  }
  void text_digits(std::uint8_t x, std::uint8_t y, std::uint8_t align,
                   const char* s) {
    const std::uint8_t len = static_cast<std::uint8_t>(std::strlen(s));
    b(sdp::op::TEXT);
    b(0x00u);  // font 0
    b(x);
    b(y);
    b(0x01u);  // palette 0
    b(align);
    b(len);
    for (std::uint8_t i = 0u; i < len; ++i) {
      b(static_cast<std::uint8_t>(s[i]));
    }
  }
};

void fmt2(char* o, std::uint8_t v) {
  o[0] = static_cast<char>('0' + (v / 10u));
  o[1] = static_cast<char>('0' + (v % 10u));
  o[2] = '\0';
}

void fmt_u32(char* o, std::size_t cap, std::uint32_t v) {
  char tmp[11];
  std::uint8_t n = 0u;
  if (v == 0u) {
    tmp[n++] = '0';
  } else {
    while (v > 0u && n < 10u) {
      tmp[n++] = static_cast<char>('0' + (v % 10u));
      v /= 10u;
    }
  }
  std::size_t i = 0u;
  while (n > 0u && i + 1u < cap) {
    o[i++] = tmp[--n];
  }
  o[i] = '\0';
}

std::size_t build_face(W& w, const ViewModel& vm) {
  const local::State& st = *vm.state;
  const clock::Civil c = clock::civil_now();

  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xFFFFu);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(0x07E0u);

  char tbuf[8];
  fmt2(tbuf, c.hour);
  tbuf[2] = ':';
  fmt2(tbuf + 3, c.minute);
  w.text_digits(120, 70, sdp::align::CENTER, tbuf);

  char dbuf[12];
  fmt2(dbuf, c.day);
  dbuf[2] = '-';
  fmt2(dbuf + 3, c.month);
  dbuf[5] = '-';
  // year mod 100
  fmt2(dbuf + 6, static_cast<std::uint8_t>(c.year % 100u));
  w.text_digits(120, 100, sdp::align::CENTER, dbuf);

  char sbuf[12];
  if (st.settings.face_show_steps) {
    fmt_u32(sbuf, sizeof(sbuf), st.steps);
    w.text_digits(120, 130, sdp::align::CENTER, sbuf);
  }

  char bbuf[8];
  fmt_u32(bbuf, sizeof(bbuf), st.battery_pct);
  w.text_digits(40, 200, sdp::align::LEFT, bbuf);

  // Link: 1 connected, 0 not — digit only (font 0).
  const char* lnk = st.link_up ? "1" : "0";
  w.text_digits(200, 200, sdp::align::RIGHT, lnk);

  if (st.remote_stale) {
    w.text_digits(120, 160, sdp::align::CENTER, "88");  // stale cue
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_notifs(W& w, const ViewModel& vm) {
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xFFFFu);

  w.text_digits(120, 8, sdp::align::CENTER, "00");  // header stand-in

  const notif::Store* ns = vm.notifs;
  const std::uint8_t count = ns ? ns->count : 0u;
  char cbuf[4];
  fmt_u32(cbuf, sizeof(cbuf), count);
  w.text_digits(120, 24, sdp::align::CENTER, cbuf);

  const std::uint8_t row_h = 28u;
  const std::uint8_t visible = count > 6u ? 6u : count;
  const std::uint16_t content_h =
      static_cast<std::uint16_t>(visible == 0u ? 1u : visible * row_h);

  w.b(sdp::op::SCROLL_REGION);
  w.b(40u);   // y
  w.b(180u);  // h
  w.u16(content_h);

  for (std::uint8_t i = 0u; i < visible; ++i) {
    const notif::Entry* e = notif::at(ns, i);
    if (e == nullptr) {
      continue;
    }
    const std::uint8_t y = static_cast<std::uint8_t>(40u + i * row_h);
    w.b(sdp::op::BEGIN_ELEM);
    w.u16(static_cast<std::uint16_t>(100u + i));
    w.b(4u);
    w.b(y);
    w.b(232u);
    w.b(static_cast<std::uint8_t>(row_h - 2u));
    w.b(sdp::elem_flags::EMIT_TOUCH);

    char line[8];
    line[0] = e->monogram >= '0' && e->monogram <= '9' ? e->monogram : '0';
    line[1] = '\0';
    w.text_digits(12, static_cast<std::uint8_t>(y + 8u), sdp::align::LEFT, line);

    // Title as digit-length cue (title_len).
    fmt_u32(line, sizeof(line), e->title_len);
    w.text_digits(40, static_cast<std::uint8_t>(y + 8u), sdp::align::LEFT, line);
    if (e->stale) {
      w.text_digits(200, static_cast<std::uint8_t>(y + 8u), sdp::align::LEFT, "1");
    }
    w.b(sdp::op::END_ELEM);
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_settings(W& w, const ViewModel& vm) {
  const local::State& st = *vm.state;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xFFFFu);

  w.text_digits(120, 20, sdp::align::CENTER, "01");  // settings id
  char s[4];
  fmt_u32(s, sizeof(s), st.settings.tilt_enabled);
  w.text_digits(40, 80, sdp::align::LEFT, s);
  fmt_u32(s, sizeof(s), st.settings.tilt_sensitivity);
  w.text_digits(40, 110, sdp::align::LEFT, s);
  fmt_u32(s, sizeof(s), st.settings.wake_seconds);
  w.text_digits(40, 140, sdp::align::LEFT, s);

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_charging(W& w, const ViewModel& vm) {
  const local::State& st = *vm.state;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0x07E0u);
  char s[8];
  fmt_u32(s, sizeof(s), st.battery_pct);
  w.text_digits(120, 100, sdp::align::CENTER, s);
  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_alert(W& w, const ViewModel& vm) {
  const local::State& st = *vm.state;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xF800u);
  char s[4];
  fmt_u32(s, sizeof(s), st.alert_kind);
  w.text_digits(120, 80, sdp::align::CENTER, s);
  fmt_u32(s, sizeof(s), st.alert_id);
  w.text_digits(120, 120, sdp::align::CENTER, s);
  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_disconnected(W& w, const ViewModel&) {
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xFFFFu);
  w.text_digits(120, 110, sdp::align::CENTER, "0");
  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

}  // namespace

std::size_t build_screen(const ViewModel& vm, std::uint8_t* out, std::size_t cap) {
  if (vm.state == nullptr || out == nullptr || cap < 8u) {
    return 0u;
  }
  W w;
  w.p = out;
  w.cap = cap;
  switch (vm.state->screen) {
    case local::Screen::Face:
      return build_face(w, vm);
    case local::Screen::Notifs:
      return build_notifs(w, vm);
    case local::Screen::Settings:
      return build_settings(w, vm);
    case local::Screen::Charging:
      return build_charging(w, vm);
    case local::Screen::Alert:
      return build_alert(w, vm);
    case local::Screen::Disconnected:
      return build_disconnected(w, vm);
  }
  return 0u;
}

}  // namespace ui
}  // namespace slate
