/**
 * Slate Camera — proves the PATCH / streaming tier (downloaded JS only).
 *
 * Design: slate.camera is a *thin controller*. Preview RGB332 frames never
 * enter this isolate — the host CameraX path downscales to 60×60, converts
 * on the phone, and pushes PATCH display lists under this app's focus.
 * Script draws chrome (shutter) once and reacts to status / shutter.
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
      b.text(0, 120, 20, 'CENTER', slate.PAL(1), 'Camera');
      b.text(0, 120, 118, 'CENTER', slate.PAL(1), status);
      if (fpsHint > 0) {
        b.text(0, 120, 136, 'CENTER', slate.PAL(1), (~ ~fpsHint) + ' fps budget');
      }
      b.element(1, 70, 170, 100, 40, function () {
        b.rectRound(70, 170, 100, 40, 12, slate.PAL(2), slate.FILL);
        b.text(0, 120, 182, 'CENTER', slate.PAL(1), 'Shutter');
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
