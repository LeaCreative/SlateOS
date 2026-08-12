/**
 * Weather JS — current conditions from the phone (Open-Meteo).
 *
 * Draws: temperature, short label, precip/wind lines; loading/error screens.
 * Does:  onFocus calls slate.weather.fetch() (last-known GPS if available);
 *        BACK stops. Events: onEvent('weather', json).
 * Perms: weather — host HTTP + optional last location; no http in the isolate.
 * Budget: a few text ops; no loops.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var mode = 'loading';
  var tempC = null;
  var label = '';
  var precip = '';
  var wind = '';
  var detail = '';

  function trunc(s, n) {
    s = String(s || '');
    if (s.length <= n) return s;
    return s.substring(0, n - 1) + '\u2026';
  }

  function statusScreen(t, sub) {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0xfd20);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 88, 'CENTER', slate.PAL(1), 2, trunc(t, 16));
      if (sub) b.textScaled(1, 120, 128, 'CENTER', slate.PAL(2), 1, trunc(sub, 28));
      b.commit();
    });
  }

  function face() {
    var t = tempC == null ? '--' : (Math.round(tempC) + 'C');
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 10, 'CENTER', slate.PAL(2), 1, 'Weather');
      b.textScaled(1, 120, 70, 'CENTER', slate.PAL(1), 4, trunc(t, 6));
      b.textScaled(1, 120, 130, 'CENTER', slate.PAL(1), 2, trunc(label, 16));
      b.textScaled(1, 120, 170, 'CENTER', slate.PAL(2), 1, trunc(precip, 22));
      b.textScaled(1, 120, 198, 'CENTER', slate.PAL(2), 1, trunc(wind, 22));
      b.commit();
    });
  }

  function screen() {
    if (mode === 'loading') return statusScreen('Weather', 'Fetching…');
    if (mode === 'error') return statusScreen('Weather failed', detail || 'Check location');
    return face();
  }

  function pushScreen() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    return [slate.weather.fetch()].concat(pushScreen());
  };

  global.render = function () { return screen(); };

  global.onBlur = function () {
    return [slate.weather.stop()];
  };

  global.onInput = function (ev) {
    if (ev.op === 0x06) {
      return [
        slate.weather.stop(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'weather') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type === 'status') {
      if (payload.state === 'loading') { mode = 'loading'; return pushScreen(); }
      if (payload.state === 'error') {
        mode = 'error';
        detail = payload.detail || '';
        return pushScreen();
      }
      return [];
    }
    if (payload.type === 'snapshot') {
      mode = 'ok';
      tempC = payload.tempC;
      label = payload.label || '';
      precip = 'Rain ' + (payload.precipMm != null ? payload.precipMm : '?') + ' mm';
      wind = 'Wind ' + (payload.windMps != null ? payload.windMps : '?') + ' m/s';
      return pushScreen();
    }
    return [];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
