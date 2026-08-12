/**
 * Health JS — today steps / HR from Health Connect + live watch buffer.
 *
 * Draws: stepsToday and hrBpm from HC fetch; optional watch snapshot.
 * Does:  onFocus fetch + watch; BACK stops.
 * Perms: health.read — HC aggregates. Watch→HC write is a companion bridge.
 * Budget: text only.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var mode = 'loading';
  var steps = null;
  var hr = null;
  var source = '';
  var detail = '';

  function trunc(s, n) {
    s = String(s || '');
    if (s.length <= n) return s;
    return s.substring(0, n - 1) + '...';
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
    var s = steps == null ? '--' : String(steps);
    var h = hr == null ? '--' : String(hr);
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 8, 'CENTER', slate.PAL(2), 1, 'Health');
      b.textScaled(1, 120, 48, 'CENTER', slate.PAL(2), 1, trunc(source || 'hc', 12));
      b.textScaled(1, 120, 100, 'CENTER', slate.PAL(1), 3, trunc(s, 8));
      b.textScaled(1, 120, 140, 'CENTER', slate.PAL(2), 1, 'steps today');
      b.textScaled(1, 120, 180, 'CENTER', slate.PAL(1), 2, trunc(h + ' bpm', 12));
      b.commit();
    });
  }

  function screen() {
    if (mode === 'loading') return statusScreen('Health', 'Reading...');
    if (mode === 'denied') return statusScreen('Need HC', detail || 'Grant Health Connect');
    if (mode === 'hc_unavailable') return statusScreen('No HC', detail || 'Install Health Connect');
    if (mode === 'error') return statusScreen('Health failed', detail);
    return face();
  }

  function push() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    detail = '';
    return push().concat([slate.health.fetch({ range: 'today' }), slate.health.watch()]);
  };

  global.onBlur = function () {
    return [slate.health.stop()];
  };

  global.onEvent = function (sourceName, data) {
    if (sourceName !== 'health') return [];
    var o;
    try { o = typeof data === 'string' ? JSON.parse(data) : data; } catch (e) { return []; }
    if (!o || !o.type) return [];
    if (o.type === 'status') {
      mode = o.state || 'error';
      detail = o.detail || '';
      return push();
    }
    if (o.type === 'snapshot') {
      mode = 'ok';
      source = o.source || '';
      if (o.stepsToday != null) steps = o.stepsToday;
      if (o.hrBpm != null) hr = o.hrBpm;
      return push();
    }
    return [];
  };

  global.onInput = function (ev) {
    if (ev && ev.op === 0x06) {
      return [
        slate.health.stop(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    return [{ type: 'inputUnhandled' }];
  };
})(this);
