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
    b(sdp::style::ModeFill);
  }
  void rect_round(std::uint8_t x, std::uint8_t y, std::uint8_t w, std::uint8_t h,
                  std::uint8_t rad, std::uint16_t c) {
    b(sdp::op::RECT_ROUND);
    b(x);
    b(y);
    b(w);
    b(h);
    b(rad);
    rgb(c);
    b(sdp::style::ModeFill);
  }
  /** Outline-only rounded rect — ModeStroke is what actually rounds corners. */
  void rect_round_stroke(std::uint8_t x, std::uint8_t y, std::uint8_t w,
                         std::uint8_t h, std::uint8_t rad, std::uint16_t c) {
    b(sdp::op::RECT_ROUND);
    b(x);
    b(y);
    b(w);
    b(h);
    b(rad);
    rgb(c);
    b(sdp::style::ModeStroke);
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

/** Top section strip: Settings | Face | Launcher. Active = face bright. */
void draw_section_bars(W& w, local::SectionId active, std::uint16_t bright,
                       std::uint16_t dim) {
  constexpr std::uint8_t kY = 2u;
  constexpr std::uint8_t kH = 3u;
  constexpr std::uint8_t kW = 28u;
  constexpr std::uint8_t kGap = 8u;
  constexpr std::uint8_t kCount = 3u;
  constexpr std::uint8_t kTotal =
      static_cast<std::uint8_t>(kCount * kW + (kCount - 1u) * kGap);
  constexpr std::uint8_t kX0 = static_cast<std::uint8_t>((240u - kTotal) / 2u);
  for (std::uint8_t i = 0u; i < kCount; ++i) {
    const std::uint8_t x =
        static_cast<std::uint8_t>(kX0 + i * (kW + kGap));
    const bool on = static_cast<std::uint8_t>(active) == i;
    w.fill(x, kY, kW, kH, on ? bright : dim);
  }
}

/**
 * Left page strip. `page_index` is 0-based; `page_count` < 2 draws nothing.
 * Vertically centred in the list band (~y 36..228).
 */
void draw_page_bars(W& w, std::uint8_t page_count, std::uint8_t page_index,
                    std::uint16_t bright, std::uint16_t dim) {
  if (page_count < 2u) {
    return;
  }
  if (page_index >= page_count) {
    page_index = 0u;
  }
  constexpr std::uint8_t kX = 2u;
  constexpr std::uint8_t kW = 3u;
  constexpr std::uint8_t kH = 14u;
  constexpr std::uint8_t kGap = 4u;
  constexpr std::uint8_t kBandMid = 132u;
  const std::uint8_t total = static_cast<std::uint8_t>(
      page_count * kH + (page_count - 1u) * kGap);
  std::uint8_t y0 = 36u;
  if (total < 180u) {
    y0 = static_cast<std::uint8_t>(kBandMid - total / 2u);
  }
  for (std::uint8_t i = 0u; i < page_count; ++i) {
    const std::uint8_t y =
        static_cast<std::uint8_t>(y0 + i * (kH + kGap));
    w.fill(kX, y, kW, kH, i == page_index ? bright : dim);
  }
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
// Line 1 — identity only. Dropped (stable since N-17 / N-36, ate width):
// reset reason, paints, stall_ms, tick_catchup, stall_events, phase/ms,
// millivolts, parse/render. Battery % is already on the face; stalls and BLE
// bring-up no longer move. Format: "<uptime s>/<chip>.<status>.<step_en>.<pwr>"
void fmt_diag(char* o, std::size_t cap, const local::State& st) {
  char part[12];
  std::size_t i = 0u;
  const auto append = [&](const char* s) {
    for (const char* p = s; *p != '\0' && i + 1u < cap; ++p) {
      o[i++] = *p;
    }
  };
  fmt_u32(part, sizeof(part), st.diag_uptime_s);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_bma_chip);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_bma_status);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_bma_step_en);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_bma_pwr);
  append(part);
  o[i] = '\0';
}

void fmt_i32(char* o, std::size_t cap, std::int32_t v) {
  if (cap == 0u) {
    return;
  }
  if (v < 0) {
    if (cap < 2u) {
      o[0] = '\0';
      return;
    }
    o[0] = '-';
    fmt_u32(o + 1, cap - 1u, static_cast<std::uint32_t>(-v));
  } else {
    fmt_u32(o, cap, static_cast<std::uint32_t>(v));
  }
}

// Line 2 — raise-to-wake only (replaces the old phase/mv/BMA and SDP lines).
// Accel shown as counts/16 so ±1g (~1024) fits in 3 digits with sign.
// Format: "<sleeps>.<samples>/<rej>.<fires>.<wake>/<x16>/<y16>/<z16>"
// Reject: 0 fire, 1 filling, 2 |x|, 3 y-var, 4 face-down, 5 y-mean, 6 roll.
// Wake: 0 none, 1 raise, 2 button, 3 double-tap, 4 charge, 5 alert, 6 shake.
void fmt_diag2(char* o, std::size_t cap, const local::State& st) {
  char part[12];
  std::size_t i = 0u;
  const auto append = [&](const char* s) {
    for (const char* p = s; *p != '\0' && i + 1u < cap; ++p) {
      o[i++] = *p;
    }
  };
  fmt_u32(part, sizeof(part), st.diag_sleep_enters);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_raise_samples);
  append(part);
  append("/");
  fmt_u32(part, sizeof(part), st.diag_raise_reject);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_raise_fires);
  append(part);
  append(".");
  fmt_u32(part, sizeof(part), st.diag_wake_src);
  append(part);
  append("/");
  fmt_i32(part, sizeof(part), st.diag_ax / 16);
  append(part);
  append("/");
  fmt_i32(part, sizeof(part), st.diag_ay / 16);
  append(part);
  append("/");
  fmt_i32(part, sizeof(part), st.diag_az / 16);
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

  const std::uint16_t kBright = st.settings.face_bright;
  const std::uint16_t kDim = st.settings.face_dim;
  constexpr std::uint16_t kAmber = 0xFD20u;
  constexpr std::uint16_t kGreen = 0x07E0u;
  constexpr std::uint16_t kGlyphTrack = 0x2124u;

  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(kBright);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(kDim);

  if (st.trial_image) {
    w.fill(0, 0, 240, 8, kAmber);
  }
  // After the trial band so the strip stays visible on an unconfirmed image.
  draw_section_bars(w, local::SectionId::Face, kBright, kDim);

#if SLATE_DIAG_OVERLAY
  if (st.settings.face_show_diag) {
    // Two short lines (identity + raise). Dropped the third SDP line — it
    // overran 240 px and the counters are in the companion log.
    char diag[40];
    fmt_diag(diag, sizeof(diag), st);
    w.text_big(4, 16, sdp::align::LEFT, 2u, 1u, diag);
    fmt_diag2(diag, sizeof(diag), st);
    w.text_big(4, 28, sdp::align::LEFT, 2u, 1u, diag);
    // Band 40..49 — OTA only while a transfer is active (I-13).
    if (st.diag_ota_shown) {
      fmt_diag_ota(diag, sizeof(diag), st);
      w.text_big(4, 40, sdp::align::LEFT, 2u, 1u, diag);
    }
    // Which image is actually running. A sealed watch has no SWD, so without
    // this the only way to tell one flash from the next is to infer it from
    // behaviour — which is exactly how we mis-attributed N-23 and N-24.
    // Gated with the rest of Face diag: the version is a diagnostic, not
    // chrome (operator: hide with Face diag Off on watch or companion).
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
    if (st.settings.hr_enabled) {
      w.text_big(20, 136, sdp::align::LEFT, 3u, 1u, sbuf);
    } else {
      w.text_big(120, 136, sdp::align::CENTER, 3u, 1u, sbuf);
    }
  }
  if (st.settings.hr_enabled) {
    char hbuf[8];
    if (st.hr_bpm == 0u) {
      hbuf[0] = '-';
      hbuf[1] = '-';
      hbuf[2] = '\0';
    } else {
      fmt_u32(hbuf, sizeof(hbuf), st.hr_bpm);
    }
    if (st.settings.face_show_steps) {
      w.text_big(220, 136, sdp::align::RIGHT, 3u, 1u, hbuf);
    } else {
      w.text_big(120, 136, sdp::align::CENTER, 3u, 1u, hbuf);
    }
  }

  w.gauge(20, 196, 200, 8,
          st.battery_pct > 100u ? 0u : st.battery_pct, kBright, kDim);

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
  w.fill(212, 212, 12, 12, st.link_up ? kGreen : kGlyphTrack);

  // Display-list activity, left of the link block. Red the moment the
  // interpreter rejects one — a rejected list and one that never arrived look
  // identical otherwise, and that is the difference between a parser bug and a
  // transport bug. Green alternates shade per applied list, so a phone that is
  // pushing shows a blink rather than a static square.
  {
    constexpr std::uint16_t kRed = 0xF800u;
    constexpr std::uint16_t kDimGreen = 0x03E0u;
    std::uint16_t dl_colour = kGlyphTrack;
    if (st.diag_dl_rej > 0u) {
      dl_colour = kRed;
    } else if (st.diag_dl_ok > 0u) {
      dl_colour = (st.diag_dl_ok & 1u) ? kGreen : kDimGreen;
    }
    w.fill(190, 212, 12, 12, dl_colour);
  }

  // Unread notification glyph (left of DL activity).
  if (vm.notifs != nullptr && vm.notifs->count > 0u) {
    constexpr std::uint16_t kNotif = 0xFD20u;  // amber
    w.fill(168, 212, 12, 12, kNotif);
  }

  if (st.remote_stale) {
    w.fill(112, 176, 16, 4, kAmber);
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_notifs(W& w, const ViewModel& vm) {
  // Outline rows + right-side page arrows (no SCROLL_REGION — vertical
  // swipe is back-to-face and must not also mean "scroll").
  constexpr std::uint8_t kFont = 1u;
  const local::State& st = *vm.state;
  const std::uint16_t chrome = st.settings.ui_chrome;
  const std::uint16_t bright = st.settings.face_bright;
  const std::uint16_t dim = st.settings.face_dim;

  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(chrome);

  // Notifications hang off the face — section strip stays on Face (middle).
  draw_section_bars(w, local::SectionId::Face, bright, dim);
  w.text_big(120, 10, sdp::align::CENTER, 2u, 0u, "Notifications", kFont);

  const notif::Store* ns = vm.notifs;
  const std::uint8_t count = ns ? ns->count : 0u;
  if (count == 0u) {
    w.text_big(120, 110, sdp::align::CENTER, 2u, 0u, "None", kFont);
    w.b(sdp::op::COMMIT);
    w.b(0x00u);
    return w.ok ? w.n : 0u;
  }

  constexpr std::uint8_t kRowX = 8u;
  constexpr std::uint8_t kRowW = 176u;  // leave strip for arrows
  constexpr std::uint8_t kRowH = 44u;
  constexpr std::uint8_t kPitch = 48u;
  constexpr std::uint8_t kListTop = 36u;
  constexpr std::uint8_t kRad = 8u;
  constexpr std::uint8_t kTextY = 15u;  // (44 - 14) / 2
  constexpr std::uint8_t kArrowX = 196u;
  constexpr std::uint8_t kArrowW = 36u;
  constexpr std::uint8_t kArrowH = 44u;

  std::uint8_t start = st.notif_sel;
  if (start >= count) {
    start = 0u;
  }
  const std::uint8_t remain =
      static_cast<std::uint8_t>(count - start);
  const std::uint8_t visible =
      remain > local::kNotifPageRows ? local::kNotifPageRows : remain;
  const std::uint8_t page_count = static_cast<std::uint8_t>(
      (count + local::kNotifPageRows - 1u) / local::kNotifPageRows);
  const std::uint8_t page_index = static_cast<std::uint8_t>(
      start / local::kNotifPageRows);
  draw_page_bars(w, page_count, page_index, bright, dim);

  for (std::uint8_t i = 0u; i < visible; ++i) {
    const std::uint8_t idx = static_cast<std::uint8_t>(start + i);
    const notif::Entry* e = notif::at(ns, idx);
    if (e == nullptr) {
      continue;
    }
    const std::uint8_t y =
        static_cast<std::uint8_t>(kListTop + i * kPitch);
    w.b(sdp::op::BEGIN_ELEM);
    w.u16(static_cast<std::uint16_t>(local::kNotifRowBase + i));
    w.b(kRowX);
    w.b(y);
    w.b(kRowW);
    w.b(kRowH);
    w.b(sdp::elem_flags::EMIT_TOUCH);
    w.rect_round_stroke(kRowX, y, kRowW, kRowH, kRad, chrome);

    char label[notif::kTitleCap + 1u];
    if (e->title_len > 0u) {
      const std::uint8_t n =
          e->title_len < notif::kTitleCap ? e->title_len : notif::kTitleCap;
      std::memcpy(label, e->title, n);
      label[n] = '\0';
    } else {
      label[0] = e->monogram;
      label[1] = '\0';
    }
    if (std::strlen(label) > 14u) {
      label[14] = '\0';
    }
    w.text_big(16, static_cast<std::uint8_t>(y + kTextY), sdp::align::LEFT, 2u,
               0u, label, kFont);
    w.b(sdp::op::END_ELEM);
  }

  // Scroll up (earlier entries).
  if (start > 0u) {
    w.b(sdp::op::BEGIN_ELEM);
    w.u16(local::kNotifScrollUp);
    w.b(kArrowX);
    w.b(40u);
    w.b(kArrowW);
    w.b(kArrowH);
    w.b(sdp::elem_flags::EMIT_TOUCH);
    w.rect_round_stroke(kArrowX, 40u, kArrowW, kArrowH, kRad, chrome);
    w.text_big(static_cast<std::uint8_t>(kArrowX + kArrowW / 2u),
               static_cast<std::uint8_t>(40u + kTextY), sdp::align::CENTER, 2u,
               0u, "^", kFont);
    w.b(sdp::op::END_ELEM);
  }

  // Scroll down (later entries).
  if (static_cast<std::uint16_t>(start) + visible < count) {
    w.b(sdp::op::BEGIN_ELEM);
    w.u16(local::kNotifScrollDown);
    w.b(kArrowX);
    w.b(176u);
    w.b(kArrowW);
    w.b(kArrowH);
    w.b(sdp::elem_flags::EMIT_TOUCH);
    w.rect_round_stroke(kArrowX, 176u, kArrowW, kArrowH, kRad, chrome);
    w.text_big(static_cast<std::uint8_t>(kArrowX + kArrowW / 2u),
               static_cast<std::uint8_t>(176u + kTextY), sdp::align::CENTER, 2u,
               0u, "v", kFont);
    w.b(sdp::op::END_ELEM);
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_notif_detail(W& w, const ViewModel& vm) {
  constexpr std::uint8_t kFont = 1u;
  const std::uint16_t chrome = vm.state->settings.ui_chrome;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(chrome);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(chrome);

  w.text_big(120, 8, sdp::align::CENTER, 2u, 0u, "DETAIL", kFont);

  if (vm.detail_pending) {
    w.text_big(120, 110, sdp::align::CENTER, 2u, 1u, "...", kFont);
  } else if (vm.detail_text != nullptr && vm.detail_text[0] != '\0') {
    // Wrap into up to 6 lines of 18 chars (scale 2).
    const char* src = vm.detail_text;
    const std::size_t total = std::strlen(src);
    constexpr std::size_t kCols = 18u;
    constexpr std::uint8_t kLineH = 22u;
    std::uint8_t line = 0u;
    std::size_t off = 0u;
    while (off < total && line < 6u) {
      char row[kCols + 1u];
      std::size_t n = total - off;
      if (n > kCols) {
        n = kCols;
      }
      std::memcpy(row, src + off, n);
      row[n] = '\0';
      w.text_big(120, static_cast<std::uint8_t>(48u + line * kLineH),
                 sdp::align::CENTER, 2u, 0u, row, kFont);
      off += n;
      ++line;
    }
  } else {
    w.text_big(120, 110, sdp::align::CENTER, 2u, 1u, "No text", kFont);
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

std::size_t build_call(W& w, const ViewModel& vm) {
  constexpr std::uint8_t kFont = 1u;
  const local::State& st = *vm.state;
  const std::uint16_t chrome = st.settings.ui_chrome;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(chrome);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(chrome);

  w.text_big(120, 48, sdp::align::CENTER, 2u, 0u, "CALL", kFont);
  const char* who = st.alert_label[0] != '\0' ? st.alert_label : "Unknown";
  char line[17];
  const std::size_t n = std::strlen(who);
  const std::size_t take = n > 16u ? 16u : n;
  std::memcpy(line, who, take);
  line[take] = '\0';
  w.text_big(120, 110, sdp::align::CENTER, 2u, 1u, line, kFont);

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

/**
 * Watch settings — four outline rows per page, swipe to page (no arrows).
 * Top section strip + left page bars match notifications / launcher.
 */
std::size_t build_settings(W& w, const ViewModel& vm) {
  const local::State& st = *vm.state;
  const std::uint16_t chrome = st.settings.ui_chrome;
  const std::uint16_t bright = st.settings.face_bright;
  const std::uint16_t dim = st.settings.face_dim;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(chrome);  // labels + outlines
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(0x07E0u);  // On
  w.b(sdp::op::SET_PALETTE);
  w.b(0x02u);
  w.u16(0xF800u);  // Off
  w.b(sdp::op::SET_PALETTE);
  w.b(0x03u);
  w.u16(0xFFE0u);  // variable values (timeout, etc.)

  constexpr std::uint8_t kFont = 1u;
  constexpr std::uint8_t kRowX = 8u;
  constexpr std::uint8_t kRowW = 224u;
  constexpr std::uint8_t kRowH = 44u;
  constexpr std::uint8_t kPitch = 48u;
  constexpr std::uint8_t kListTop = 36u;
  constexpr std::uint8_t kRad = 8u;
  constexpr std::uint8_t kTextY = 15u;  // (44 - 14) / 2 for scale-2 5×7
  constexpr std::uint8_t kPalOn = 1u;
  constexpr std::uint8_t kPalOff = 2u;
  constexpr std::uint8_t kPalVar = 3u;

  draw_section_bars(w, local::SectionId::Settings, bright, dim);
  w.text_big(120, 10, sdp::align::CENTER, 2u, 0u, "Settings", kFont);

  std::uint8_t start = st.settings_sel;
  if (start >= local::kSettingsRowCount) {
    start = 0u;
  }
  start = static_cast<std::uint8_t>(
      (start / local::kSettingsPageRows) * local::kSettingsPageRows);
  const std::uint8_t remain =
      static_cast<std::uint8_t>(local::kSettingsRowCount - start);
  const std::uint8_t visible = remain > local::kSettingsPageRows
                                   ? local::kSettingsPageRows
                                   : remain;
  const std::uint8_t page_count = static_cast<std::uint8_t>(
      (local::kSettingsRowCount + local::kSettingsPageRows - 1u) /
      local::kSettingsPageRows);
  const std::uint8_t page_index =
      static_cast<std::uint8_t>(start / local::kSettingsPageRows);
  draw_page_bars(w, page_count, page_index, bright, dim);

  const auto sens_label = [](std::uint8_t s) -> const char* {
    switch (s) {
      case 0u:
        return "Soft";
      case 2u:
        return "Hard";
      default:
        return "Normal";
    }
  };

  char timeout_buf[8];
  if (st.settings.wake_seconds == 0u) {
    timeout_buf[0] = 'N';
    timeout_buf[1] = 'e';
    timeout_buf[2] = 'v';
    timeout_buf[3] = 'e';
    timeout_buf[4] = 'r';
    timeout_buf[5] = '\0';
  } else {
    fmt_u32(timeout_buf, sizeof(timeout_buf) - 1u, st.settings.wake_seconds);
    std::size_t l = 0u;
    while (timeout_buf[l] != '\0' && l + 1u < sizeof(timeout_buf)) {
      ++l;
    }
    if (l + 1u < sizeof(timeout_buf)) {
      timeout_buf[l] = 's';
      timeout_buf[l + 1u] = '\0';
    }
  }

  const auto fill_row = [&](std::uint8_t abs, std::uint16_t* id,
                            const char** label, const char** value,
                            std::uint8_t* pal) {
    switch (abs) {
      case 0u:
        *id = local::kSettingRaise;
        *label = "Raise wake";
        *value = st.settings.tilt_enabled ? "On" : "Off";
        *pal = st.settings.tilt_enabled ? kPalOn : kPalOff;
        break;
      case 1u:
        *id = local::kSettingRaiseSens;
        *label = "Raise sens";
        *value = sens_label(st.settings.raise_sensitivity);
        *pal = kPalVar;
        break;
      case 2u:
        *id = local::kSettingShake;
        *label = "Shake wake";
        *value = st.settings.shake_enabled ? "On" : "Off";
        *pal = st.settings.shake_enabled ? kPalOn : kPalOff;
        break;
      case 3u:
        *id = local::kSettingShakeSens;
        *label = "Shake sens";
        *value = sens_label(st.settings.shake_sensitivity);
        *pal = kPalVar;
        break;
      case 4u:
        *id = local::kSettingTimeout;
        *label = "Timeout";
        *value = timeout_buf;
        *pal = kPalVar;
        break;
      case 5u:
        *id = local::kSettingSteps;
        *label = "Show steps";
        *value = st.settings.face_show_steps ? "On" : "Off";
        *pal = st.settings.face_show_steps ? kPalOn : kPalOff;
        break;
      case 6u:
        *id = local::kSettingDiag;
        *label = "Face diag";
        *value = st.settings.face_show_diag ? "On" : "Off";
        *pal = st.settings.face_show_diag ? kPalOn : kPalOff;
        break;
      case 7u:
        *id = local::kSettingHr;
        *label = "Heart rate";
        *value = st.settings.hr_enabled ? "On" : "Off";
        *pal = st.settings.hr_enabled ? kPalOn : kPalOff;
        break;
      default:
        *id = local::kSettingHaptic;
        *label = "Vibrations";
        *value = st.settings.haptic_enabled ? "On" : "Off";
        *pal = st.settings.haptic_enabled ? kPalOn : kPalOff;
        break;
    }
  };

  for (std::uint8_t i = 0u; i < visible; ++i) {
    const std::uint8_t abs = static_cast<std::uint8_t>(start + i);
    std::uint16_t id = 0u;
    const char* label = "";
    const char* value = "";
    std::uint8_t pal = kPalVar;
    fill_row(abs, &id, &label, &value, &pal);
    const std::uint8_t y =
        static_cast<std::uint8_t>(kListTop + i * kPitch);
    w.b(sdp::op::BEGIN_ELEM);
    w.u16(id);
    w.b(kRowX);
    w.b(y);
    w.b(kRowW);
    w.b(kRowH);
    w.b(sdp::elem_flags::EMIT_TOUCH | sdp::elem_flags::HAPTIC);
    w.rect_round_stroke(kRowX, y, kRowW, kRowH, kRad, chrome);
    w.text_big(16, static_cast<std::uint8_t>(y + kTextY), sdp::align::LEFT, 2u,
               0u, label, kFont);
    w.text_big(224, static_cast<std::uint8_t>(y + kTextY), sdp::align::RIGHT,
               2u, pal, value, kFont);
    w.b(sdp::op::END_ELEM);
  }

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
std::size_t build_disconnected(W& w, const ViewModel& vm) {
  constexpr std::uint8_t kFont5x7 = 1u;
  const std::uint16_t chrome =
      vm.state != nullptr ? vm.state->settings.ui_chrome : 0xFFFFu;
  w.b(sdp::op::CLEAR);
  w.rgb(0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(chrome);
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
#if SLATE_DIAG_OVERLAY
// The three diagnostic lines on their own, without touching the rest of the
// face.
//
// The overlay refreshes every 2 s, and it used to do that by repainting the
// WHOLE screen: 240x240 is 115200 B of SPI plus thirty tile passes that each
// re-parse the display list, measured at 222-238 ms. Roughly a 12 % duty cycle
// spent redrawing a clock that had not changed, in order to update three lines
// of text — and it lands on the app task, which is also what drains the SDP
// inbox and reads the touch panel (N-36).
//
// Emits no CLEAR, exactly like build_ota_banner: the renderer culls tiles the
// list never touches (N-18), so this costs ~5 tile passes instead of 30. The
// band is filled first because without a CLEAR the previous text would still
// be there underneath.
std::size_t build_diag_banner(std::uint8_t* out, std::size_t cap,
                              const local::State& st) {
  if (out == nullptr || cap < 8u) {
    return 0u;
  }
  W w;
  w.p = out;
  w.cap = cap;

  // Two lines at y=16 and 28 (raise campaign). Clock starts at 56.
  constexpr std::uint8_t kTop = 16u;
  constexpr std::uint8_t kHeight = 24u;
  const std::uint16_t kDim = st.settings.face_dim;

  w.fill(0, kTop, 240, kHeight, 0x0000u);
  // Palette 1 must be set even though the face sets it too: this list is
  // parsed on its own, and the interpreter rejects an unset palette entry.
  w.b(sdp::op::SET_PALETTE);
  w.b(0x01u);
  w.u16(kDim);

  char diag[40];
  fmt_diag(diag, sizeof(diag), st);
  w.text_big(4, 16, sdp::align::LEFT, 2u, 1u, diag);
  fmt_diag2(diag, sizeof(diag), st);
  w.text_big(4, 28, sdp::align::LEFT, 2u, 1u, diag);
  if (st.diag_ota_shown) {
    w.fill(0, 40, 240, 10, 0x0000u);
    fmt_diag_ota(diag, sizeof(diag), st);
    w.text_big(4, 40, sdp::align::LEFT, 2u, 1u, diag);
  }

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}
#endif

// HH:MM on its own, for the minute rollover.
//
// The rollover repainted the entire face once a minute, for ever, on every
// watch: ~236 ms of SPI and thirty tile passes to change at most four glyphs.
// The digits occupy y 56..95 and nothing else on the face changes when the
// minute does, so this is the same band trick the diag lines and the OTA
// banner already use — no CLEAR, so untouched tiles are culled (N-18).
std::size_t build_clock_band(std::uint8_t* out, std::size_t cap,
                             std::uint16_t face_bright) {
  if (out == nullptr || cap < 8u) {
    return 0u;
  }
  W w;
  w.p = out;
  w.cap = cap;

  // Scale 8 on the 3x5 font gives 24x40 glyphs drawn from y=56.
  constexpr std::uint8_t kY = 56u;
  constexpr std::uint8_t kH = 40u;

  w.fill(0, kY, 240, kH, 0x0000u);
  w.b(sdp::op::SET_PALETTE);
  w.b(0x00u);
  w.u16(face_bright);

  const clock::Civil c = clock::civil_now();
  char tbuf[8];
  fmt2(tbuf, c.hour);
  tbuf[2] = ':';
  fmt2(tbuf + 3, c.minute);
  w.text_big(120, kY, sdp::align::CENTER, 8u, 0u, tbuf);

  w.b(sdp::op::COMMIT);
  w.b(0x00u);
  return w.ok ? w.n : 0u;
}

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
    case local::Screen::NotifDetail:
      return build_notif_detail(w, vm);
    case local::Screen::Settings:
      return build_settings(w, vm);
    case local::Screen::Charging:
      return build_charging(w, vm);
    case local::Screen::Alert:
      return build_alert(w, vm);
    case local::Screen::Call:
      return build_call(w, vm);
    case local::Screen::Disconnected:
      return build_disconnected(w, vm);
  }
  return 0u;
}

}  // namespace ui
}  // namespace slate
