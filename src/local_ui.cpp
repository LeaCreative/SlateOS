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
  void text_big(std::uint8_t x, std::uint8_t y, std::uint8_t align,
                std::uint8_t scale, std::uint8_t palette, const char* s) {
    const std::uint8_t len = static_cast<std::uint8_t>(std::strlen(s));
    b(sdp::op::TEXT_SCALED);
    u16(static_cast<std::uint16_t>(7u + len));
    b(0x00u);  // font 0
    b(x);
    b(y);
    b(static_cast<std::uint8_t>(sdp::color_tag::PaletteMin + palette));
    b(align);
    b(scale);
    b(len);
    for (std::uint8_t i = 0u; i < len; ++i) {
      b(static_cast<std::uint8_t>(s[i]));
    }
  }
  void fill(std::uint8_t x, std::uint8_t y, std::uint8_t w, std::uint8_t h,
            std::uint16_t c) {
    b(sdp::op::RECT);
    b(x);
    b(y);
    b(w);
    b(h);
    rgb(c);
    b(0x00u);  // STYLE: fill, width 0
  }
  void gauge(std::uint8_t x, std::uint8_t y, std::uint8_t w, std::uint8_t h,
             std::uint8_t pct, std::uint16_t fg, std::uint16_t bg) {
    b(sdp::op::PROGRESS_BAR);
    b(x);
    b(y);
    b(w);
    b(h);
    b(pct > 100u ? 100u : pct);
    rgb(fg);
    rgb(bg);
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

// Temporary bring-up readout. Build with -DSLATE_DIAG_OVERLAY=0 to drop it.
#ifndef SLATE_DIAG_OVERLAY
#define SLATE_DIAG_OVERLAY 1
#endif

#if SLATE_DIAG_OVERLAY
// "<reset reason>/<uptime s>/<paints>/<button>/<worst app stall ms>/<ble
// state>.<ble rc>/<recovered ticks>", decimal
// because font 0 has no A-F glyphs (codepoints 45..58 only). Reason is the raw
// RESETREAS bitmask: 0 power-on or brownout, 1 pin, 2 watchdog, 4 soft,
// 8 lockup, 65536 off-wake, 1048576 VBUS; several causes sum.
void fmt_diag(char* o, std::size_t cap, const local::State& st) {
  char part[12];
  std::size_t i = 0u;
  const auto append = [&](const char* s) {
    for (const char* p = s; *p != '\0' && i + 1u < cap; ++p) {
      o[i++] = *p;
    }
  };
  fmt_u32(part, sizeof(part), st.diag_reset_reason);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_uptime_s);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_paints);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_button);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_stall_ms);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_ble_state);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_ble_rc);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_tick_catchup);
  append(part);
  o[i] = '\0';
}

// Second bring-up line:
// "<worst phase>.<its ms>/<adc raw>/<battery mV>/<parse ms>.<render ms>".
// Phase ids in local_state.hpp; 6 (the notify wait) running long means the app
// task was starved rather than slow.
void fmt_diag2(char* o, std::size_t cap, const local::State& st) {
  char part[12];
  std::size_t i = 0u;
  const auto append = [&](const char* s) {
    for (const char* p = s; *p != '\0' && i + 1u < cap; ++p) {
      o[i++] = *p;
    }
  };
  fmt_u32(part, sizeof(part), st.diag_phase);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_phase_ms);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_adc_raw);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_mv);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_parse_ms);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_render_ms);
  append(part);
  o[i] = '\0';
}
#endif

// Layout is fixed for the 240x240 panel. Vertical bands, top to bottom:
//   0..7    trial-image marker (only while unconfirmed)
//   16..25  bring-up diagnostics (SLATE_DIAG_OVERLAY)
//   56..95  HH:MM at scale 8 (24x40 glyphs)
//   110..124 date at scale 3
//   136..150 steps at scale 3
//   196..203 battery gauge
//   212..226 battery percent, and the link indicator at the right
std::size_t build_face(W& w, const ViewModel& vm) {
  const local::State& st = *vm.state;
  const clock::Civil c = clock::civil_now();

  constexpr std::uint16_t kWhite = 0xFFFFu;
  constexpr std::uint16_t kDim = 0x8410u;
  constexpr std::uint16_t kAmber = 0xFD20u;
  constexpr std::uint16_t kGreen = 0x07E0u;
  constexpr std::uint16_t kTrack = 0x2124u;

  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(kWhite);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(kDim);

  if (st.trial_image) {
    w.fill(0, 0, 240, 8, kAmber);
  }

#if SLATE_DIAG_OVERLAY
  // Scale 2 and left-aligned: four numbers can exceed 240 px at scale 3, and a
  // centred run that wide would start at a negative x and be rejected.
  char diag[40];
  fmt_diag(diag, sizeof(diag), st);
  w.text_big(4, 16, sdp::align::LEFT, 2u, 1u, diag);
  // Band 28..37 — still clear of the clock at y=56.
  fmt_diag2(diag, sizeof(diag), st);
  w.text_big(4, 28, sdp::align::LEFT, 2u, 1u, diag);
#endif

  char tbuf[8];
  fmt2(tbuf, c.hour);
  tbuf[2] = ':';
  fmt2(tbuf + 3, c.minute);
  w.text_big(120, 56, sdp::align::CENTER, 8u, 0u, tbuf);

  char dbuf[12];
  fmt2(dbuf, c.day);
  dbuf[2] = '-';
  fmt2(dbuf + 3, c.month);
  dbuf[5] = '-';
  // year mod 100
  fmt2(dbuf + 6, static_cast<std::uint8_t>(c.year % 100u));
  w.text_big(120, 110, sdp::align::CENTER, 3u, 1u, dbuf);

  char sbuf[12];
  if (st.settings.face_show_steps) {
    fmt_u32(sbuf, sizeof(sbuf), st.steps);
    w.text_big(120, 136, sdp::align::CENTER, 3u, 1u, sbuf);
  }

  w.gauge(20, 196, 200, 8,
          st.battery_pct > 100u ? 0u : st.battery_pct, kWhite, kTrack);

  char bbuf[8];
  if (st.battery_pct > 100u) {
    bbuf[0] = '-';
    bbuf[1] = '-';
    bbuf[2] = '\0';
  } else {
    fmt_u32(bbuf, sizeof(bbuf), st.battery_pct);
  }
  w.text_big(20, 212, sdp::align::LEFT, 3u, 1u, bbuf);

  if (st.charging) {
    // Compact lightning bolt next to the percent (not a full-screen takeover).
    constexpr std::uint16_t kCharge = 0x07E0u;
    w.fill(78, 212, 3, 6, kCharge);
    w.fill(81, 215, 8, 3, kCharge);
    w.fill(86, 218, 3, 8, kCharge);
    w.fill(81, 221, 8, 3, kCharge);
  }

  // Link state as a colour block: a lone 0/1 digit was unreadable and ambiguous.
  w.fill(212, 212, 12, 12, st.link_up ? kGreen : kTrack);

  if (st.remote_stale) {
    w.fill(112, 176, 16, 4, kAmber);
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_notifs(W& w, const ViewModel& vm) {
  // Scale 3 body (9×15 glyphs). Header/count short enough to centre; row
  // fields left-aligned. Six rows × ~44 B TEXT_SCALED elems fit in the
  // 512-byte local dl_buf_ (~300 B measured worst case).
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xFFFFu);

  w.text_big(120, 4, sdp::align::CENTER, 3u, 0u, "00");  // screen id

  const notif::Store* ns = vm.notifs;
  const std::uint8_t count = ns ? ns->count : 0u;
  char cbuf[4];
  fmt_u32(cbuf, sizeof(cbuf), count);
  w.text_big(120, 24, sdp::align::CENTER, 3u, 0u, cbuf);

  constexpr std::uint8_t kRowH = 28u;  // scale-3 glyph 15 + margin
  constexpr std::uint8_t kMaxVisible = 6u;
  const std::uint8_t visible = count > kMaxVisible ? kMaxVisible : count;
  const std::uint16_t content_h =
      static_cast<std::uint16_t>(visible == 0u ? 1u : visible * kRowH);

  constexpr std::uint8_t kScrollY = 48u;
  w.b(sdp::op::SCROLL_REGION);
  w.b(kScrollY);
  w.b(180u);  // h
  w.u16(content_h);

  for (std::uint8_t i = 0u; i < visible; ++i) {
    const notif::Entry* e = notif::at(ns, i);
    if (e == nullptr) {
      continue;
    }
    const std::uint8_t y =
        static_cast<std::uint8_t>(kScrollY + i * kRowH);
    w.b(sdp::op::BEGIN_ELEM);
    w.u16(static_cast<std::uint16_t>(100u + i));
    w.b(4u);
    w.b(y);
    w.b(232u);
    w.b(static_cast<std::uint8_t>(kRowH - 2u));
    w.b(sdp::elem_flags::EMIT_TOUCH);

    char line[8];
    line[0] = e->monogram >= '0' && e->monogram <= '9' ? e->monogram : '0';
    line[1] = '\0';
    w.text_big(8, static_cast<std::uint8_t>(y + 6u), sdp::align::LEFT, 3u, 0u,
               line);

    // Title length cue (numeric only — font 0 has no letters).
    fmt_u32(line, sizeof(line), e->title_len);
    w.text_big(40, static_cast<std::uint8_t>(y + 6u), sdp::align::LEFT, 3u, 0u,
               line);
    if (e->stale) {
      w.text_big(200, static_cast<std::uint8_t>(y + 6u), sdp::align::LEFT, 3u,
                 0u, "1");
    }
    w.b(sdp::op::END_ELEM);
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_settings(W& w, const ViewModel& vm) {
  // Body at scale 3, left-aligned (values can be multi-digit).
  const local::State& st = *vm.state;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xFFFFu);

  w.text_big(120, 16, sdp::align::CENTER, 3u, 0u, "01");  // settings id
  char s[4];
  fmt_u32(s, sizeof(s), st.settings.tilt_enabled);
  w.text_big(16, 64, sdp::align::LEFT, 3u, 0u, s);
  fmt_u32(s, sizeof(s), st.settings.tilt_sensitivity);
  w.text_big(16, 100, sdp::align::LEFT, 3u, 0u, s);
  fmt_u32(s, sizeof(s), st.settings.wake_seconds);
  w.text_big(16, 136, sdp::align::LEFT, 3u, 0u, s);

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_charging(W& w, const ViewModel& vm) {
  // Kept for Screen::Charging if anything still navigates there; poll_battery
  // no longer switches to it. Same face layout with the charge icon.
  return build_face(w, vm);
}

std::size_t build_alert(W& w, const ViewModel& vm) {
  // Kind at clock hierarchy (scale 8); id at body (scale 3).
  const local::State& st = *vm.state;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xF800u);
  char s[4];
  fmt_u32(s, sizeof(s), st.alert_kind);
  w.text_big(120, 72, sdp::align::CENTER, 8u, 0u, s);
  fmt_u32(s, sizeof(s), st.alert_id);
  w.text_big(120, 140, sdp::align::CENTER, 3u, 0u, s);
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
  w.text_big(120, 100, sdp::align::CENTER, 8u, 0u, "0");
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
