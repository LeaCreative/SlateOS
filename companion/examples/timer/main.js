/**
 * Timer — countdown with start/stop, on the watch.
 *
 * Draws: MM:SS at scale 6, one full-width button beneath it.
 * Does:  tap toggles running; a 1 s host timer decrements; buzzes the watch
 *        motor at zero. Remaining time and running state survive a blur.
 * Perms: storage — persistence, and reading the durationSec setting.
 * Setting: durationSec (5-3600 s, default 60) sets the starting value. Read at
 *        focus only, so a change applies the next time the app is opened.
 * Budget: 63 B display list, 4 drawing ops (measured). Far inside every limit
 *        in docs/subapp-rules.md. No loops; MM:SS is fixed-width.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var DEFAULT_SECONDS = 60;
  var remaining = DEFAULT_SECONDS;
  var running = false;

  /**
   * Configured start value.
   *
   * Settings arrive as ordinary store entries, so this must defend against a
   * missing or malformed value exactly as docs/subapp-rules.md §5.2 requires —
   * the store is also writable by this script.
   */
  function configuredSeconds() {
    var raw = slate.store.get('durationSec');
    var n = parseInt(raw, 10);
    if (isNaN(n) || n < 5 || n > 3600) return DEFAULT_SECONDS;
    return n;
  }

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
      b.textScaled(0, 120, 70, 'CENTER', slate.PAL(1), 6, label);
      b.element(1, 40, 160, 160, 40, function () {
        b.rectRound(40, 160, 160, 40, 8, slate.PAL(2), slate.FILL);
        b.textScaled(0, 120, 168, 'CENTER', slate.PAL(0), 3, btn);
      });
      b.commit();
    });
  }

  global.onFocus = function () {
    // Start from the configured duration, then let a saved countdown override
    // it so re-focusing mid-run does not restart the timer.
    remaining = configuredSeconds();
    var s = slate.store.get('remaining');
    if (s != null && s !== '') {
      var n = parseInt(s, 10);
      if (!isNaN(n) && n > 0) remaining = n;
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
        slate.store.set('remaining', String(configuredSeconds())),
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
