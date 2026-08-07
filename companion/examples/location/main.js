/**
 * Where Am I — the phone's position, on the watch.
 *
 * Draws: title, latitude and longitude at scale 3, an accuracy line, a Refresh
 *        button, and a status line along the bottom. Before the first fix the
 *        coordinates are replaced by a single explanatory line, because
 *        "0.00000 / 0.00000" reads as a real position off West Africa and is
 *        the wrong thing to show while searching. Font 1 (5x7) throughout.
 * Does:  onFocus subscribes to the location adapter at the configured
 *        interval; onBlur and BACK (0x06) unsubscribe, so the GPS is not held
 *        on behind a screen nobody is looking at. Tapping Refresh asks for a
 *        single immediate fix. Fixes and status changes arrive as
 *        onEvent('location', json) and redraw the screen.
 * Perms: location — slate.location.* is gated on it. Note this permission is
 *        in PermissionPolicy.THIRD_PARTY_BLOCKED, so a downloaded third-party
 *        app gets it only if the user grants it explicitly in the repository
 *        screen; this demo has it because it is bundled at Official trust.
 *        storage — reads the updateSec setting. §5.2 requires the storage
 *        permission to read settings. Nothing is written.
 * Settings: updateSec (1-300 s, default 5) is how often the phone is asked for
 *        a fix. Read at focus, so a change applies next time the app opens.
 *        The host floors the interval at 1 s regardless of what is asked for.
 * Budget: 157 B worst case (a fix at three-digit longitude with four-digit
 *        accuracy), 8 drawing ops — measured. 125 B while searching. No loops;
 *        every string is fixed-width or bounded by toFixed. Well inside every
 *        limit in docs/subapp-rules.md.
 *
 * The four status states are not decoration. "denied" and "disabled" are
 * terminal — no fix is ever coming, and only the user can change that — so
 * this app says which one applies rather than sitting on "searching" forever.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var DEFAULT_UPDATE_SEC = 5;
  var MIN_UPDATE_SEC = 1;
  var MAX_UPDATE_SEC = 300;
  var COORD_DECIMALS = 5;   // ~1 m; more than the watch can usefully show.

  var fix = null;
  var status = 'searching';

  /** What the bottom line says for each state the host can report. */
  function statusText() {
    if (status === 'denied') return 'No permission';
    if (status === 'disabled') return 'Location is off';
    if (status === 'unavailable') return 'No GPS on phone';
    if (fix) return 'Fix ' + (fix.provider || '?');
    return 'Searching...';
  }

  /** True when the user has to do something on the phone before this works. */
  function isTerminal() {
    return status === 'denied' || status === 'disabled' || status === 'unavailable';
  }

  function helpText() {
    if (status === 'denied') return 'Grant location in Slate';
    if (status === 'disabled') return 'Turn on phone location';
    if (status === 'unavailable') return 'This phone cannot';
    return 'Waiting for a fix';
  }

  /**
   * Configured update interval, defended.
   *
   * Settings arrive as ordinary store entries and this script could overwrite
   * one, so anything outside the declared range falls back to the default
   * (docs/subapp-rules.md §5.2).
   */
  function configuredUpdateSec() {
    var n = parseInt(slate.store.get('updateSec'), 10);
    if (isNaN(n) || n < MIN_UPDATE_SEC || n > MAX_UPDATE_SEC) return DEFAULT_UPDATE_SEC;
    return n;
  }

  function face() {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);   // black
      b.palette(1, 0xffff);   // white
      b.palette(2, 0x07ff);   // cyan — coordinates
      b.palette(3, 0xfd20);   // amber — something needs attention
      b.palette(4, 0x001f);   // blue — button
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 8, 'CENTER', slate.PAL(1), 2, 'Location');

      if (fix) {
        b.textScaled(1, 120, 40, 'CENTER', slate.PAL(2), 3,
          fix.lat.toFixed(COORD_DECIMALS));
        b.textScaled(1, 120, 72, 'CENTER', slate.PAL(2), 3,
          fix.lon.toFixed(COORD_DECIMALS));
        b.textScaled(1, 120, 108, 'CENTER', slate.PAL(1), 2,
          '+/- ' + (fix.accuracyM | 0) + ' m');
      } else {
        b.textScaled(1, 120, 60, 'CENTER', slate.PAL(3), 2, helpText());
      }

      b.element(1, 40, 150, 160, 44, function () {
        b.rectRound(40, 150, 160, 44, 8, slate.PAL(4), slate.FILL);
        b.textScaled(1, 120, 164, 'CENTER', slate.PAL(1), 2, 'Refresh');
      });

      b.textScaled(1, 120, 212, 'CENTER',
        isTerminal() ? slate.PAL(3) : slate.PAL(1), 2, statusText());
      b.commit();
    });
  }

  global.onFocus = function () {
    fix = null;
    status = 'searching';
    var sec = configuredUpdateSec();
    slate.log('info', 'location: subscribe every ' + sec + 's');
    return [
      slate.location.subscribe({ minIntervalMs: sec * 1000 }),
      { type: 'pushDisplayList', displayListBase64: face() }
    ];
  };

  global.render = function () {
    return face();
  };

  global.onBlur = function () {
    // Releasing the subscription here matters more than it looks: a location
    // stream left running behind a screen nobody can see is a battery drain
    // the user has no way to notice.
    return [slate.location.unsubscribe()];
  };

  global.onInput = function (ev) {
    if (ev.elemId === 1) {
      return [
        slate.location.request(),
        { type: 'inputHandled' }
      ];
    }
    if (ev.op === 0x06) {
      return [
        slate.location.unsubscribe(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'location') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type === 'status') {
      status = payload.state || status;
      // A terminal status invalidates any fix already on screen: the stream
      // has stopped and what is shown is no longer being updated.
      if (isTerminal()) fix = null;
    } else if (payload.type === 'fix') {
      // A fix without coordinates is not a fix. Defaulting them to 0 would
      // draw 0.00000 / 0.00000 — a real-looking position in the Gulf of
      // Guinea — with nothing to say it was never a position at all.
      if (typeof payload.lat !== 'number' || typeof payload.lon !== 'number') {
        return [];
      }
      fix = {
        lat: payload.lat,
        lon: payload.lon,
        accuracyM: typeof payload.accuracyM === 'number' ? payload.accuracyM : 0,
        provider: payload.provider || ''
      };
      status = 'ok';
    } else {
      return [];
    }
    return [
      slate.invalidate(),
      { type: 'pushDisplayList', displayListBase64: face() }
    ];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
