/**
 * Sample Slate sub-app: buzz the phone from the watch.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 *
 * The point of this one is the direction of travel: every other demo pushes
 * pixels to the watch, this reaches back and makes the *phone* do something.
 * Note the distinction from slate.haptic(), which buzzes the watch's own
 * motor — this is slate.phone.vibrate(), gated behind the "vibrate"
 * permission, and it is the phone in your pocket that answers.
 */
(function (global) {
  'use strict';

  var PATTERNS = [
    { label: 'Short', ms: 150 },
    { label: 'Long', ms: 800 },
    { label: 'Triple', ms: 0, pulses: 3 }
  ];
  var selected = 0;
  var sent = 0;

  function screen() {
    var p = PATTERNS[selected];
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x001f);
      b.palette(3, 0x8410);
      b.clear(slate.PAL(0));

      // Font 1 is the 5x7 — legible at scale 2. Font 0 has no lowercase worth
      // reading at this size.
      b.textScaled(1, 120, 14, 'CENTER', slate.PAL(1), 2, 'Buzz Phone');

      // Big button: buzz now.
      b.element(1, 20, 44, 200, 92, function () {
        b.rectRound(20, 44, 200, 92, 10, slate.PAL(2), slate.FILL);
        b.textScaled(1, 120, 72, 'CENTER', slate.PAL(1), 3, p.label);
        b.textScaled(1, 120, 104, 'CENTER', slate.PAL(1), 2, 'Tap to buzz');
      });

      // Cycle the pattern.
      b.element(2, 20, 148, 200, 44, function () {
        b.rectRound(20, 148, 200, 44, 8, slate.PAL(3), slate.FILL);
        b.textScaled(1, 120, 162, 'CENTER', slate.PAL(0), 2, 'Next pattern');
      });

      b.textScaled(1, 120, 208, 'CENTER', slate.PAL(1), 2, 'Sent ' + sent);
      b.commit();
    });
  }

  function buzz() {
    var p = PATTERNS[selected];
    var out = [];
    if (p.pulses) {
      for (var i = 0; i < p.pulses; i++) out.push(slate.phone.vibrate(120));
    } else {
      out.push(slate.phone.vibrate(p.ms));
    }
    return out;
  }

  global.onFocus = function () {
    sent = 0;
    slate.log('info', 'vibrate: focus');
    return screen();
  };

  global.render = function () {
    return screen();
  };

  global.onInput = function (ev) {
    if (ev.elemId === 1) {
      sent++;
      var out = buzz();
      out.push({ type: 'inputHandled' });
      out.push(slate.invalidate());
      out.push({ type: 'pushDisplayList', displayListBase64: screen() });
      return out;
    }
    if (ev.elemId === 2) {
      selected = (selected + 1) % PATTERNS.length;
      return [
        { type: 'inputHandled' },
        slate.invalidate(),
        { type: 'pushDisplayList', displayListBase64: screen() }
      ];
    }
    // 0x06 is BACK — hand the screen back to whatever was under us.
    if (ev.op === 0x06) {
      return [{ type: 'relinquishFocus' }, { type: 'inputHandled' }];
    }
    return [{ type: 'inputUnhandled' }];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
