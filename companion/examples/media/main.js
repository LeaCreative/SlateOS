/**
 * Media JS — now-playing on the watch.
 *
 * Draws: title/artist lines, Play/Pause and Next outline buttons; idle/denied
 *        status screens when nothing is playing or NLS is off.
 * Does:  onFocus subscribes to slate.media; taps send play/pause/next; BACK
 *        unsubscribes. Events arrive as onEvent('media', json).
 * Perms: media — MediaSession via Notification Listener on the phone.
 * Budget: ~4 outline buttons + text; no loops over metadata.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var mode = 'loading'; // loading | playing | idle | denied | error
  var playing = false;
  var title = '';
  var artist = '';
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

  function nowPlayingScreen() {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 8, 'CENTER', slate.PAL(2), 1, 'Media');
      b.textScaled(1, 120, 40, 'CENTER', slate.PAL(1), 2, trunc(title || '(no title)', 18));
      b.textScaled(1, 120, 72, 'CENTER', slate.PAL(2), 1, trunc(artist || '', 22));
      b.element(1, 16, 150, 100, 44, function () {
        b.rectRound(16, 150, 100, 44, 8, slate.PAL(1), 1);
        b.textScaled(1, 66, 164, 'CENTER', slate.PAL(1), 2, playing ? 'Pause' : 'Play');
      });
      b.element(2, 124, 150, 100, 44, function () {
        b.rectRound(124, 150, 100, 44, 8, slate.PAL(1), 1);
        b.textScaled(1, 174, 164, 'CENTER', slate.PAL(1), 2, 'Next');
      });
      b.commit();
    });
  }

  function screen() {
    if (mode === 'denied') return statusScreen('Need NLS', 'Notification access');
    if (mode === 'error') return statusScreen('Media failed', detail || 'Check phone');
    if (mode === 'idle') return statusScreen('Nothing playing', 'Start media on phone');
    if (mode === 'loading') return statusScreen('Media', 'Waiting…');
    return nowPlayingScreen();
  }

  function pushScreen() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    return [slate.media.subscribe()].concat(pushScreen());
  };

  global.render = function () { return screen(); };

  global.onBlur = function () {
    return [slate.media.unsubscribe()];
  };

  global.onInput = function (ev) {
    if (ev.op === 0x06) {
      return [
        slate.media.unsubscribe(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    if (mode === 'playing' || mode === 'idle') {
      if (ev.elemId === 1) {
        return [(playing ? slate.media.pause() : slate.media.play()), { type: 'inputHandled' }];
      }
      if (ev.elemId === 2) {
        return [slate.media.next(), { type: 'inputHandled' }];
      }
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'media') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type === 'status') {
      if (payload.state === 'idle') { mode = 'idle'; return pushScreen(); }
      if (payload.state === 'denied') { mode = 'denied'; return pushScreen(); }
      if (payload.state === 'error') {
        mode = 'error';
        detail = payload.detail || '';
        return pushScreen();
      }
      return [];
    }
    if (payload.type === 'nowPlaying') {
      mode = 'playing';
      playing = !!payload.playing;
      title = payload.title || '';
      artist = payload.artist || '';
      return pushScreen();
    }
    return [];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
