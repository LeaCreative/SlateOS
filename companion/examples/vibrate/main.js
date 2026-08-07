/**
 * Buzz Phone — buzz the *phone* from the watch.
 *
 * The point of this one is the direction of travel: every other demo pushes
 * pixels to the watch, this reaches back and makes the phone do something.
 *
 * Draws: title, one large button carrying the selected pattern and its
 *        duration, a smaller button that cycles the pattern, and a sent count.
 *        Font 1 (5x7) throughout — font 0 is a 3x5 cell and is not legible at
 *        arm's length on a 240x240 panel.
 * Does:  tap the big button -> slate.phone.vibrate(ms) on the handset, and the
 *        count increments; tap the lower button -> next pattern; BACK (0x06)
 *        relinquishes focus. Nothing happens on the watch itself —
 *        slate.haptic() would buzz the watch motor and is deliberately unused.
 * Perms: vibrate — slate.phone.vibrate() is the whole app, and it is gated on
 *        this permission.
 *        storage — reads the two duration settings. §5.2 requires the storage
 *        permission to read settings. Nothing is ever written.
 * Settings: shortMs and longMs (20-2000 ms; 150 / 800) set the two durations.
 *        Read at focus, so a change applies next time the app is opened.
 * Budget: 143 B display list, 8 drawing ops — measured, not estimated. No
 *        loops. Far inside every limit in docs/subapp-rules.md.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var DEFAULT_SHORT_MS = 150;
  var DEFAULT_LONG_MS = 800;
  // slate.phone.vibrate() clamps to 0-2000 ms in shared-js/slate_host.js.
  // Bounding the settings to the same range keeps the number the user sets and
  // the number the phone gets identical — otherwise 3000 would silently be 2000.
  var MIN_MS = 20;
  var MAX_MS = 2000;

  // A 'Triple' pattern was here and did not work: three vibrate() calls a
  // couple of milliseconds apart each cancel the previous one, so the phone
  // buzzes once. A real repeating pattern needs a single waveform action
  // (VibrationEffect.createWaveform) rather than N one-shots.
  var PATTERNS = [
    { label: 'Short', ms: DEFAULT_SHORT_MS },
    { label: 'Long', ms: DEFAULT_LONG_MS }
  ];
  var selected = 0;
  var sent = 0;

  /**
   * One duration setting, defended.
   *
   * Settings arrive as ordinary store entries and the store is writable by
   * this script, so the value can be anything — docs/subapp-rules.md §5.2.
   */
  function settingMs(key, fallback) {
    var n = parseInt(slate.store.get(key), 10);
    if (isNaN(n) || n < MIN_MS || n > MAX_MS) return fallback;
    return n;
  }

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

      // Big button: buzz now. The duration is on the face so the setting is
      // observable on hardware — the operator cannot read the store.
      b.element(1, 20, 44, 200, 92, function () {
        b.rectRound(20, 44, 200, 92, 10, slate.PAL(2), slate.FILL);
        b.textScaled(1, 120, 72, 'CENTER', slate.PAL(1), 3, p.label);
        b.textScaled(1, 120, 104, 'CENTER', slate.PAL(1), 2, p.ms + ' ms');
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
    return [slate.phone.vibrate(PATTERNS[selected].ms)];
  }

  global.onFocus = function () {
    // Settings are read here and only here (§5.2): a change takes effect the
    // next time the app is opened, never mid-session.
    PATTERNS[0].ms = settingMs('shortMs', DEFAULT_SHORT_MS);
    PATTERNS[1].ms = settingMs('longMs', DEFAULT_LONG_MS);
    sent = 0;
    slate.log('info', 'vibrate: focus short=' + PATTERNS[0].ms + ' long=' + PATTERNS[1].ms);
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
