/**
 * Alarms JS — schedule a phone alarm from the watch.
 *
 * Draws: +15m / +1h buttons; status line.
 * Does:  taps call slate.alarms.set; backend is companion HostPrefs (clock|exact).
 * Perms: alarms (+ storage for label setting).
 * Budget: three buttons.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var status = 'Pick delay';
  var detail = '';

  function trunc(s, n) {
    s = String(s || '');
    if (s.length <= n) return s;
    return s.substring(0, n - 1) + '...';
  }

  function label() {
    var s = slate.store.get('label');
    return (s && String(s)) || 'Slate';
  }

  function screen() {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.palette(3, 0xfd20);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 8, 'CENTER', slate.PAL(2), 1, 'Alarms');
      b.textScaled(1, 120, 40, 'CENTER', slate.PAL(1), 1, trunc(status, 22));
      if (detail) b.textScaled(1, 120, 64, 'CENTER', slate.PAL(3), 1, trunc(detail, 28));
      b.element(1, 20, 100, 200, 40, function () {
        b.rectRound(20, 100, 200, 40, 6, slate.PAL(2), slate.FILL);
        b.textScaled(1, 120, 112, 'CENTER', slate.PAL(1), 2, '+15 min');
      });
      b.element(2, 20, 160, 200, 40, function () {
        b.rectRound(20, 160, 200, 40, 6, slate.PAL(2), slate.FILL);
        b.textScaled(1, 120, 172, 'CENTER', slate.PAL(1), 2, '+1 hour');
      });
      b.commit();
    });
  }

  function push() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    status = 'Pick delay';
    detail = '';
    return push();
  };

  global.onBlur = function () {
    return [slate.alarms.stop()];
  };

  global.onInput = function (ev) {
    if (ev && ev.op === 0x06) {
      return [
        slate.alarms.stop(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    if (!ev || ev.type !== 'tap') return [{ type: 'inputUnhandled' }];
    var id = ev.elemId | 0;
    var mins = id === 1 ? 15 : (id === 2 ? 60 : 0);
    if (!mins) return [{ type: 'inputUnhandled' }];
    var whenMs = Date.now() + mins * 60 * 1000;
    status = 'Scheduling...';
    detail = '';
    return push().concat([
      slate.alarms.set({ whenMs: whenMs, label: label() })
    ]);
  };

  global.onEvent = function (source, data) {
    if (source !== 'alarms') return [];
    var o;
    try { o = typeof data === 'string' ? JSON.parse(data) : data; } catch (e) { return []; }
    if (!o || !o.type) return [];
    if (o.type === 'scheduled') {
      status = 'Scheduled';
      detail = String(o.backend || '') + ' ok';
      return push();
    }
    if (o.type === 'status') {
      status = o.state || 'error';
      detail = o.detail || '';
      return push();
    }
    if (o.type === 'list') {
      var n = (o.items && o.items.length) || 0;
      if (n) detail = n + ' exact armed';
      return push();
    }
    return [];
  };
})(this);
