/**
 * Sample Slate sub-app: countdown timer.
 * Downloaded JavaScript only — never dex/JAR/.so.
 * Exercises render, input, timers, and storage persistence.
 */
(function (global) {
  'use strict';

  var remaining = 60;
  var running = false;

  function pad2(n) {
    n = n | 0;
    return (n < 10 ? '0' : '') + n;
  }

  function face() {
    var label = pad2((remaining / 60) | 0) + ':' + pad2(remaining % 60);
    var btn = running ? 'Stop' : 'Start';
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x07e0);
      b.clear(slate.PAL(0));
      b.text(0, 120, 80, 'CENTER', slate.PAL(1), label);
      b.element(1, 40, 160, 160, 40, function () {
        b.rectRound(40, 160, 160, 40, 8, slate.PAL(2), slate.FILL);
        b.text(0, 120, 172, 'CENTER', slate.PAL(0), btn);
      });
      b.commit();
    });
  }

  global.onFocus = function () {
    var s = slate.store.get('remaining');
    if (s != null && s !== '') {
      var n = parseInt(s, 10);
      if (!isNaN(n)) remaining = n;
    }
    var r = slate.store.get('running');
    running = r === '1';
    slate.log('info', 'timer focus remaining=' + remaining);
    return face();
  };

  global.render = function () {
    return face();
  };

  global.onBlur = function () {
    return [slate.store.set('remaining', String(remaining)), slate.store.set('running', running ? '1' : '0')];
  };

  global.onInput = function (ev) {
    if (ev.elemId === 1 || (ev.op === 0x01 && ev.y >= 160)) {
      running = !running;
      var out = [
        slate.store.set('remaining', String(remaining)),
        slate.store.set('running', running ? '1' : '0'),
        { type: 'inputHandled' }
      ];
      if (running) {
        out.push(slate.timer.set('tick', 1000));
      } else {
        out.push(slate.timer.clear('tick'));
      }
      out.push(slate.invalidate());
      out.push({ type: 'pushDisplayList', displayListBase64: face() });
      return out;
    }
    if (ev.op === 0x06) {
      return [{ type: 'relinquishFocus' }, { type: 'inputHandled' }];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'timer') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.id !== 'tick') return [];
    if (!running) return [];
    remaining = Math.max(0, remaining - 1);
    if (remaining === 0) {
      running = false;
      return [
        slate.timer.clear('tick'),
        slate.store.set('remaining', '0'),
        slate.store.set('running', '0'),
        slate.haptic(1),
        slate.invalidate(),
        { type: 'pushDisplayList', displayListBase64: face() }
      ];
    }
    return [
      slate.store.set('remaining', String(remaining)),
      slate.invalidate(),
      { type: 'pushDisplayList', displayListBase64: face() }
    ];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
