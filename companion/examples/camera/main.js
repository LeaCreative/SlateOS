/**
 * Camera — viewfinder chrome for the host-side PATCH / streaming tier.
 *
 * slate.camera is a *thin controller*. Preview RGB332 frames never enter this
 * isolate: the host CameraX path downscales to 60x60, converts on the phone,
 * and pushes PATCH display lists under this app's focus. This script only
 * draws the chrome around the hole and reacts to status events.
 *
 * Draws: a title, an outlined 64x64 preview hole at (88,38) — one pixel of
 *        margin around the 60x60 patch the host writes at (90,40) — a status
 *        line, an optional fps-budget line, and a Shutter button. Font 1
 *        (5x7) at scale 2; the 3x5 base font is not legible at arm's length.
 * Does:  onFocus starts the host preview and pushes the chrome; the Shutter
 *        element (or a tap below y=170) calls capture() and buzzes the watch
 *        motor; BACK (0x06) stops the preview and relinquishes focus; onBlur
 *        stops it too, so leaving by any route releases the camera. Status and
 *        capture events from the host redraw the chrome.
 * Perms: camera — start/stop/capture are gated on it (declare in the manifest;
 *        third-party packages may use it the same way as Official).
 *        No storage: nothing is persisted and no settings are declared.
 * Settings: none. The only numbers worth tuning are the preview rect, and they
 *        must match the geometry the host overlays its PATCH at — a user-set
 *        value that disagreed would misalign the picture, not customise it.
 * Budget: 91 B idle, 115 B worst case (streaming, with the fps line), 6-7
 *        drawing ops — measured. The host's own PATCH frames are far larger
 *        and are not this app's list; they ride the privileged streaming path.
 *        No loops.
 *
 * Downloaded JavaScript only — never dex/JAR/.so. The camera itself is driven
 * by host Kotlin; nothing native is loaded here.
 */
(function (global) {
  'use strict';

  var status = 'idle';
  var fpsHint = 0;

  function chrome() {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0xf800);
      b.clear(slate.PAL(0));
      // Preview hole — host overlays PATCH at (90,40) 60×60.
      b.rect(88, 38, 64, 64, slate.PAL(1), 1);
      // Font 1 (5x7) scaled, as the timer and vibrate demos use. Font 0 is a
      // 3x5 cell: it carries the full printable ASCII set, but at one pixel
      // per cell pixel on a 240x240 panel it is not readable at arm's length.
      b.textScaled(1, 120, 14, 'CENTER', slate.PAL(1), 2, 'Camera');
      b.textScaled(1, 120, 112, 'CENTER', slate.PAL(1), 2, status);
      if (fpsHint > 0) {
        b.textScaled(1, 120, 134, 'CENTER', slate.PAL(1), 2, (~ ~fpsHint) + ' fps budget');
      }
      b.element(1, 70, 170, 100, 40, function () {
        b.rectRound(70, 170, 100, 40, 12, slate.PAL(2), slate.FILL);
        b.textScaled(1, 120, 180, 'CENTER', slate.PAL(1), 2, 'Shutter');
      });
      b.commit();
    });
  }

  global.onFocus = function () {
    status = 'starting';
    return [
      slate.camera.start({ slot: 0, x: 90, y: 40, w: 60, h: 60 }),
      { type: 'pushDisplayList', displayListBase64: chrome() }
    ];
  };

  global.render = function () {
    return chrome();
  };

  global.onBlur = function () {
    status = 'idle';
    return [slate.camera.stop()];
  };

  global.onInput = function (ev) {
    if (ev.elemId === 1 || (ev.op === 0x05 && ev.y >= 170)) {
      return [
        slate.camera.capture(),
        slate.haptic(1),
        { type: 'inputHandled' }
      ];
    }
    if (ev.op === 0x06) {
      return [slate.camera.stop(), { type: 'relinquishFocus' }, { type: 'inputHandled' }];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'camera') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type === 'status') {
      status = payload.state || status;
      fpsHint = payload.fpsHint || fpsHint;
      return [slate.invalidate(), { type: 'pushDisplayList', displayListBase64: chrome() }];
    }
    if (payload.type === 'captured') {
      status = 'captured';
      return [slate.haptic(2), slate.invalidate(), { type: 'pushDisplayList', displayListBase64: chrome() }];
    }
    // Frames are host-pushed — script ignores type=frame if ever sent.
    return [];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
