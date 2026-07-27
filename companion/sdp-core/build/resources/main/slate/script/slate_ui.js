/**
 * slate.ui — JS display-list builder.
 * Emits the SAME SDP bytes as Kotlin DisplayListBuilder / firmware opcodes.
 * Loaded into every isolate / Rhino context before sub-app code.
 */
(function (global) {
  'use strict';

  function u8(buf, v) { buf.push(v & 0xff); }
  function u16le(buf, v) { u8(buf, v); u8(buf, v >> 8); }

  var OP = {
    CLEAR: 0x01, SET_PALETTE: 0x02, RECT: 0x03, RECT_ROUND: 0x04,
    LINE: 0x05, CIRCLE: 0x06, ARC: 0x07, POLYLINE: 0x08,
    CLIP_RECT: 0x09, CLIP_CLEAR: 0x0A,
    TEXT: 0x10, TEXT_BOX: 0x11, ICON: 0x12, IMAGE: 0x13,
    PROGRESS_BAR: 0x20, PROGRESS_ARC: 0x21,
    BEGIN_ELEM: 0x30, END_ELEM: 0x31, SCROLL_REGION: 0x40,
    PATCH: 0x50, HAPTIC: 0x60, BACKLIGHT: 0x61,
    COMMIT: 0xF0, RETAIN: 0xF1
  };

  function colorTag(buf, c) {
    if (typeof c === 'number') {
      // palette index 0..15 encoded as tag 0x01..0x10
      u8(buf, (c & 0x0f) + 1);
    } else if (c && c.tag === 'rgb') {
      u8(buf, 0x00);
      u16le(buf, c.v & 0xffff);
    } else if (c && c.tag === 'pal') {
      u8(buf, (c.i & 0x0f) + 1);
    } else {
      u8(buf, 0x01); // pal 0 fallback
    }
  }

  function Builder() {
    this.buf = [];
  }
  Builder.prototype.palette = function (idx, rgb565) {
    u8(this.buf, OP.SET_PALETTE);
    u8(this.buf, idx);
    u16le(this.buf, rgb565 & 0xffff);
  };
  Builder.prototype.clear = function (c) {
    u8(this.buf, OP.CLEAR);
    colorTag(this.buf, c);
  };
  Builder.prototype.rect = function (x, y, w, h, c, style) {
    u8(this.buf, OP.RECT);
    u8(this.buf, x); u8(this.buf, y); u8(this.buf, w); u8(this.buf, h);
    colorTag(this.buf, c);
    u8(this.buf, style == null ? 0 : style);
  };
  Builder.prototype.rectRound = function (x, y, w, h, r, c, style) {
    u8(this.buf, OP.RECT_ROUND);
    u8(this.buf, x); u8(this.buf, y); u8(this.buf, w); u8(this.buf, h); u8(this.buf, r);
    colorTag(this.buf, c);
    u8(this.buf, style == null ? 0 : style);
  };
  Builder.prototype.text = function (font, x, y, align, c, s) {
    var f = (typeof font === 'string') ? 0 : (font | 0);
    var a = 0;
    if (align === 'CENTER' || align === 1) a = 1;
    else if (align === 'RIGHT' || align === 2) a = 2;
    var bytes = [];
    for (var i = 0; i < s.length; i++) bytes.push(s.charCodeAt(i) & 0xff);
    u8(this.buf, OP.TEXT);
    u8(this.buf, f); u8(this.buf, x); u8(this.buf, y);
    colorTag(this.buf, c);
    u8(this.buf, a);
    u8(this.buf, bytes.length);
    for (var j = 0; j < bytes.length; j++) u8(this.buf, bytes[j]);
  };
  Builder.prototype.element = function (id, x, y, w, h, flagsOrFn, maybeFn) {
    var flags = 0;
    var fn = flagsOrFn;
    if (typeof flagsOrFn === 'number') { flags = flagsOrFn; fn = maybeFn; }
    u8(this.buf, OP.BEGIN_ELEM);
    u16le(this.buf, id); u8(this.buf, x); u8(this.buf, y); u8(this.buf, w); u8(this.buf, h); u8(this.buf, flags);
    if (typeof fn === 'function') fn(this);
    u8(this.buf, OP.END_ELEM);
  };
  Builder.prototype.retain = function (ttl) {
    u8(this.buf, OP.RETAIN);
    u16le(this.buf, ttl | 0);
  };
  Builder.prototype.commit = function () {
    u8(this.buf, OP.COMMIT);
    u8(this.buf, 0);
  };
  Builder.prototype.toBytes = function () { return this.buf.slice(); };

  function bytesToBase64(bytes) {
    var binary = '';
    for (var i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i] & 0xff);
    if (typeof btoa === 'function') return btoa(binary);
    // Rhino / limited: manual base64
    var alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    var out = '';
    for (var i = 0; i < bytes.length; i += 3) {
      var a = bytes[i] & 0xff;
      var b = i + 1 < bytes.length ? bytes[i + 1] & 0xff : 0;
      var c = i + 2 < bytes.length ? bytes[i + 2] & 0xff : 0;
      var n = (a << 16) | (b << 8) | c;
      out += alphabet[(n >> 18) & 63];
      out += alphabet[(n >> 12) & 63];
      out += (i + 1 < bytes.length) ? alphabet[(n >> 6) & 63] : '=';
      out += (i + 2 < bytes.length) ? alphabet[n & 63] : '=';
    }
    return out;
  }

  var slate = global.slate || {};
  slate.PAL = function (i) { return { tag: 'pal', i: i | 0 }; };
  slate.rgb = function (v) { return { tag: 'rgb', v: v | 0 }; };
  slate.FILL = 0;
  slate.ui = {
    displayList: function (fn) {
      var b = new Builder();
      fn(b);
      return bytesToBase64(b.toBytes());
    }
  };
  global.slate = slate;
})(typeof globalThis !== 'undefined' ? globalThis : this);
