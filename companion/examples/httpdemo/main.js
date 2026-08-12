/**
 * HTTP JS — proves slate.http GET with allowlisted host.
 *
 * Draws: status + first ~80 chars of the response body (or error).
 * Does:  onFocus GETs the url setting; BACK cancels. onEvent('http', …).
 * Perms: http + storage (url setting). Host must be in http.allowedHosts.
 * Budget: text only; body truncated in JS.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var mode = 'loading';
  var line1 = '';
  var line2 = '';
  var detail = '';

  function trunc(s, n) {
    s = String(s || '');
    if (s.length <= n) return s;
    return s.substring(0, n - 1) + '\u2026';
  }

  function url() {
    var raw = slate.store.get('url');
    return raw == null ? '' : String(raw).trim();
  }

  function screen() {
    var title = mode === 'error' ? 'HTTP failed' : (mode === 'ok' ? 'HTTP OK' : 'HTTP');
    var sub = mode === 'error' ? (detail || 'error') : (mode === 'loading' ? 'Fetching…' : line1);
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0xfd20);
      b.palette(3, 0x8410);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 20, 'CENTER', slate.PAL(1), 2, trunc(title, 16));
      b.textScaled(1, 8, 70, 'LEFT', slate.PAL(2), 1, trunc(sub, 28));
      if (line2) b.textScaled(1, 8, 100, 'LEFT', slate.PAL(3), 1, trunc(line2, 28));
      b.commit();
    });
  }

  function pushScreen() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    line1 = '';
    line2 = '';
    var u = url();
    if (!u) {
      mode = 'error';
      detail = 'Set URL in settings';
      return pushScreen();
    }
    return [slate.http.get(u, { id: 'demo' })].concat(pushScreen());
  };

  global.render = function () { return screen(); };

  global.onBlur = function () {
    return [slate.http.stop()];
  };

  global.onInput = function (ev) {
    if (ev.op === 0x06) {
      return [
        slate.http.stop(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'http') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type === 'error') {
      mode = 'error';
      detail = payload.detail || 'error';
      return pushScreen();
    }
    if (payload.type === 'response') {
      mode = 'ok';
      var body = String(payload.body || '');
      line1 = 'HTTP ' + (payload.status | 0);
      line2 = body.substring(0, 80);
      return pushScreen();
    }
    return [];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
