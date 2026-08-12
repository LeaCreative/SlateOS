/**
 * Weather JS — current conditions + sea level / tides near the phone.
 *
 * Draws: temperature, label, precip, wind, sea level, next high/low tide.
 * Does:  onFocus calls slate.weather.fetch() using the phone GPS; BACK stops.
 *        Events: onEvent('weather', json) with snapshot fields including
 *        seaLevelM / nextHighTime / nextHighM / nextLowTime / nextLowM.
 * Perms: weather — host Open-Meteo forecast + marine; location via host GPS.
 * Budget: text only; no loops over data in JS.
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
  var sea = '';
  var high = '';
  var low = '';
  var detail = '';

  function trunc(s, n) {
    s = String(s || '');
    if (s.length <= n) return s;
    return s.substring(0, n - 1) + '...';
  }

  function fmtM(v) {
    if (v == null || typeof v !== 'number' || !isFinite(v)) return '?';
    var x = Math.round(v * 100) / 100;
    return String(x);
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
      b.textScaled(1, 120, 4, 'CENTER', slate.PAL(2), 1, 'Weather');
      b.textScaled(1, 120, 48, 'CENTER', slate.PAL(1), 4, trunc(t, 6));
      b.textScaled(1, 120, 100, 'CENTER', slate.PAL(1), 2, trunc(label, 16));
      b.textScaled(1, 120, 128, 'CENTER', slate.PAL(2), 1, trunc(precip, 22));
      b.textScaled(1, 120, 148, 'CENTER', slate.PAL(2), 1, trunc(wind, 22));
      b.textScaled(1, 120, 172, 'CENTER', slate.PAL(2), 1, trunc(sea, 22));
      b.textScaled(1, 120, 192, 'CENTER', slate.PAL(2), 1, trunc(high, 22));
      b.textScaled(1, 120, 212, 'CENTER', slate.PAL(2), 1, trunc(low, 22));
      b.commit();
    });
  }

  function screen() {
    if (mode === 'loading') return statusScreen('Weather', 'Fetching...');
    if (mode === 'error') return statusScreen('Weather failed', detail || 'Check location');
    return face();
  }

  function pushScreen() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    sea = '';
    high = '';
    low = '';
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
      if (payload.seaLevelM != null && isFinite(payload.seaLevelM)) {
        sea = 'Sea ' + fmtM(payload.seaLevelM) + ' m';
      } else {
        sea = 'Sea n/a';
      }
      if (payload.nextHighTime) {
        high = 'High ' + payload.nextHighTime;
        if (payload.nextHighM != null && isFinite(payload.nextHighM)) {
          high += ' ' + fmtM(payload.nextHighM) + 'm';
        }
      } else {
        high = 'High n/a';
      }
      if (payload.nextLowTime) {
        low = 'Low ' + payload.nextLowTime;
        if (payload.nextLowM != null && isFinite(payload.nextLowM)) {
          low += ' ' + fmtM(payload.nextLowM) + 'm';
        }
      } else {
        low = 'Low n/a';
      }
      return pushScreen();
    }
    return [];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
