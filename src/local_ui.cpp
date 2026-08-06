#include "local_ui.hpp"

#include "sdp_opcodes.hpp"
#include "slate_version.hpp"
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
  // font: 0 = 3x5 (dense — the diagnostic overlay lives on it), 1 = 5x7
  // (legible — anything a person is meant to read).
  void text_big(std::uint8_t x, std::uint8_t y, std::uint8_t align,
                std::uint8_t scale, std::uint8_t palette, const char* s,
                std::uint8_t font = 0u) {
    const std::uint8_t len = static_cast<std::uint8_t>(std::strlen(s));
    b(sdp::op::TEXT_SCALED);
    u16(static_cast<std::uint16_t>(7u + len));
    b(font);
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
  // Dropped: raw button level and the BLE bring-up stage/rc. Both were
  // bring-up instruments — the button reads 0 except while held, and the BLE
  // pair has read 7.0 on every build since N-17. Freed room for the SDP
  // counters on line 3, which earn their space.
  fmt_u32(part, sizeof(part), st.diag_stall_ms);
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
  // ADC raw dropped: line 2 outgrew the 240 px panel once the dl/touch/face
  // counters were added. Millivolts is the derived value that actually gets
  // checked against a multimeter; the raw count can come back if needed.
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

// Third line — the SDP path end to end, so a lost display list can be placed
// exactly:
//   "<frame>.<inbox>/<applied>.<rejected>.<dropped>.<state>/
//    <irq>.<readfail>.<touch>.<hit>"
//
// The twi::Status field that lived at the end was removed once N-31 closed:
// readfail sits at 0 and the line needs the width. cst816s::last_twi_status()
// still exists if a future failure needs it back.
//
// Read left to right it follows the message: frame reassembly, then the app
// inbox, then the session, then the interpreter. The first non-zero drop
// counter is where it died. Split out of line 2, which ran off the panel.
void fmt_diag3(char* o, std::size_t cap, const local::State& st) {
  char part[12];
  std::size_t i = 0u;
  const auto append = [&](const char* s2) {
    for (const char* p2 = s2; *p2 != '\0' && i + 1u < cap; ++p2) {
      o[i++] = *p2;
    }
  };
  fmt_u32(part, sizeof(part), st.diag_frame_drop);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_inbox_drop);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_dl_ok);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_dl_rej);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_dl_drop);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_sess_state);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_touch_irq);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_touch_readfail);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_touch);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_touch_hit);
  append(part);
  o[i] = '\0';
}

// OTA line: "<percent>/<last nak reason>/<nak count>" (I-13). Reasons are the
// slate::ota::NakReason values — 0 ok, 1 busy, 2 bad message, 3 hash fail,
// 4 too large, 5 no storage, 6 low battery, 7 yield, 8 image unconfirmed.
void fmt_diag_ota(char* o, std::size_t cap, const local::State& st) {
  char part[12];
  std::size_t i = 0u;
  const auto append = [&](const char* s) {
    for (const char* p = s; *p != '\0' && i + 1u < cap; ++p) {
      o[i++] = *p;
    }
  };
  fmt_u32(part, sizeof(part), st.diag_ota_pct);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_ota_nak);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_ota_naks);
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
  // SDP path counters get their own band; combined with line 2 they overran
  // the 240 px panel and the right-hand fields were simply invisible.
  fmt_diag3(diag, sizeof(diag), st);
  w.text_big(4, 40, sdp::align::LEFT, 2u, 1u, diag);
  // Band 40..49 — OTA only, and only once a transfer has been begun or
  // refused, so the idle face keeps its cheaper repaint (I-13).
  if (st.diag_ota_shown) {
    fmt_diag_ota(diag, sizeof(diag), st);
    w.text_big(4, 40, sdp::align::LEFT, 2u, 1u, diag);
  }
  // Which image is actually running. A sealed watch has no SWD, so without
  // this the only way to tell one flash from the next is to infer it from
  // behaviour — which is exactly how we mis-attributed N-23 and N-24.
  {
    char ver[40];
    std::size_t vi = 0u;
    const auto put = [&](const char* s) {
      for (const char* p = s; *p != '\0' && vi + 1u < sizeof(ver); ++p) {
        ver[vi++] = *p;
      }
    };
    put(slate::version::kVersion);
    put(" ");
    // "Mmm dd yyyy" -> "Mmm dd", then HH:MM from "hh:mm:ss".
    for (std::size_t k = 0u; k < 6u && slate::version::kBuildDate[k] != '\0';
         ++k) {
      if (vi + 1u < sizeof(ver)) {
        ver[vi++] = slate::version::kBuildDate[k];
      }
    }
    put(" ");
    for (std::size_t k = 0u; k < 5u && slate::version::kBuildTime[k] != '\0';
         ++k) {
      if (vi + 1u < sizeof(ver)) {
        ver[vi++] = slate::version::kBuildTime[k];
      }
    }
    ver[vi] = '\0';
    // Scale 2, matching the diag lines — scale 1 is legible on a bench but
    // not on a wrist or in a photograph.
    w.text_big(4, 178, sdp::align::LEFT, 2u, 1u, ver);
  }
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

  // Display-list activity, left of the link block. Red the moment the
  // interpreter rejects one — a rejected list and one that never arrived look
  // identical otherwise, and that is the difference between a parser bug and a
  // transport bug. Green alternates shade per applied list, so a phone that is
  // pushing shows a blink rather than a static square.
  {
    constexpr std::uint16_t kRed = 0xF800u;
    constexpr std::uint16_t kDimGreen = 0x03E0u;
    std::uint16_t dl_colour = kTrack;
    if (st.diag_dl_rej > 0u) {
      dl_colour = kRed;
    } else if (st.diag_dl_ok > 0u) {
      dl_colour = (st.diag_dl_ok & 1u) ? kGreen : kDimGreen;
    }
    w.fill(190, 212, 12, 12, dl_colour);
  }

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

// Shown when the wearer asks for something only the phone can provide — today
// that is the app drawer, reached by swiping right-to-left — while no
// companion is connected. Until font 1 existed this screen drew a single "0",
// because the built-in font had no letters to say anything with.
//
// Three centred lines at scale 2: the 5x7 advances 6, so scale 2 is 12 px per
// character and 20 characters is the widest line that fits the 240 px panel.
std::size_t build_disconnected(W& w, const ViewModel&) {
  constexpr std::uint8_t kFont5x7 = 1u;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(0xFFFFu);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(0xFD20u);  // amber — this is a prompt, not an error
  w.text_big(120, 74, sdp::align::CENTER, 3u, 1u, "Not connected", kFont5x7);
  w.text_big(120, 116, sdp::align::CENTER, 2u, 0u, "Please connect the", kFont5x7);
  w.text_big(120, 136, sdp::align::CENTER, 2u, 0u, "Slate OS", kFont5x7);
  w.text_big(120, 156, sdp::align::CENTER, 2u, 0u, "companion app", kFont5x7);
  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

}  // namespace

// Deliberately NOT a full screen: no CLEAR, and everything lands inside one
// 240x40 band. The renderer culls tiles with no content (N-18), so this costs
// ~5 tile passes instead of 30 — cheap enough to draw during a transfer, when
// the app task is otherwise standing back off the SPI bus (N-22).
std::size_t build_ota_banner(std::uint8_t* out, std::size_t cap,
                             std::uint8_t pct) {
  if (out == nullptr || cap < 8u) {
    return 0u;
  }
  W w;
  w.p = out;
  w.cap = cap;

  // Lower status bar, in the band between the version line and the battery
  // gauge. A bar plus a glyph that alternates every repaint: the bar says how
  // far along, the blink says it is still moving. A stalled transfer is then
  // obvious at a glance, which a percentage alone does not give you.
  // y=154: the gap between the step count (136..150) and the version line
  // (178..192). The band this used to occupy, 186..195, is now under the
  // scale-2 version line — so the banner was drawing into occupied pixels.
  constexpr std::uint8_t kY = 154u;
  constexpr std::uint16_t kGreen = 0x07E0u;
  constexpr std::uint16_t kTrack = 0x2124u;

  w.fill(0, kY, 240, 10, 0x0000u);
  w.gauge(20, kY + 2u, 180, 6, pct > 100u ? 100u : pct, kGreen, kTrack);
  // Parity of the percentage drives the blink — the banner repaints every 2%.
  w.fill(210, kY + 2u, 8, 6, ((pct / 2u) & 1u) ? kGreen : kTrack);

  // Without COMMIT the interpreter parses the list and never presents it, so
  // the banner was built correctly and drawn nowhere — which is why it never
  // appeared however the transfer was started.
  w.b(sdp::op::COMMIT);
  w.b(0x00u);

  return w.ok ? w.n : 0u;
}

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
