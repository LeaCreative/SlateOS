/**
 * Calendar JS — next events from the phone calendar.
 *
 * Draws: up to 5 upcoming titles with start times.
 * Does:  onFocus → slate.calendar.fetch(); BACK stops.
 * Perms: calendar — CalendarContract on the phone.
 * Budget: text rows only.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var mode = 'loading';
  var items = [];
  var detail = '';

  function trunc(s, n) {
    s = String(s || '');
    if (s.length <= n) return s;
    return s.substring(0, n - 1) + '...';
  }

  function fmtTime(ms) {
    var d = new Date(ms);
    var h = d.getHours();
    var m = d.getMinutes();
    return (h < 10 ? '0' : '') + h + ':' + (m < 10 ? '0' : '') + m;
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

  function listScreen() {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 4, 'CENTER', slate.PAL(2), 1, 'Calendar');
      var y = 36;
      var i;
      for (i = 0; i < items.length && i < 5; i++) {
        var it = items[i];
        var line = trunc((it.allDay ? 'ALL ' : fmtTime(it.startMs) + ' ') + (it.title || ''), 22);
        b.textScaled(1, 8, y, 'LEFT', slate.PAL(1), 1, line);
        y += 28;
      }
      if (items.length === 0) {
        b.textScaled(1, 120, 100, 'CENTER', slate.PAL(2), 1, 'No upcoming');
      }
      b.commit();
    });
  }

  function screen() {
    if (mode === 'loading') return statusScreen('Calendar', 'Fetching...');
    if (mode === 'denied') return statusScreen('Need calendar', detail || 'Grant in Permissions');
    if (mode === 'error') return statusScreen('Calendar failed', detail);
    return listScreen();
  }

  function pushScreen() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    items = [];
    detail = '';
    return pushScreen().concat([slate.calendar.fetch({ limit: 5 })]);
  };

  global.onBlur = function () {
    return [slate.calendar.stop()];
  };

  global.onEvent = function (source, data) {
    if (source !== 'calendar') return [];
    var o;
    try { o = typeof data === 'string' ? JSON.parse(data) : data; } catch (e) { return []; }
    if (!o || !o.type) return [];
    if (o.type === 'status') {
      mode = o.state === 'denied' ? 'denied' : (o.state === 'loading' ? 'loading' : 'error');
      detail = o.detail || '';
      return pushScreen();
    }
    if (o.type === 'events') {
      mode = 'list';
      items = o.items || [];
      return pushScreen();
    }
    return [];
  };

  global.onInput = function (ev) {
    if (ev && ev.op === 0x06) {
      return [
        slate.calendar.stop(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    return [{ type: 'inputUnhandled' }];
  };
})(this);
