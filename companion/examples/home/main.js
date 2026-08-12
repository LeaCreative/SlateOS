/**
 * Home JS — toggle / brightness for Home Assistant entities.
 *
 * Draws: up to 4 entity rows (tap = toggle; Dim/Bright for lights).
 * Does:  onFocus refresh; tap → slate.home.toggle / set.
 * Perms: home + storage (baseUrl, token, entities settings).
 * Budget: few text rows; host owns HTTP.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var mode = 'loading';
  var entities = [];
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
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.palette(3, 0x07e0);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 4, 'CENTER', slate.PAL(2), 1, 'Home');
      var y = 32;
      var i;
      for (i = 0; i < entities.length && i < 4; i++) {
        (function (idx, yy) {
          var e = entities[idx];
          var on = e.state === 'on' || e.state === 'open';
          var line = trunc((on ? '[ON] ' : '[--] ') + (e.name || e.id), 20);
          b.element(1 + idx, 8, yy, 224, 36, function () {
            b.rectRound(8, yy, 224, 36, 4, slate.PAL(2), slate.FILL);
            b.textScaled(1, 16, yy + 10, 'LEFT', slate.PAL(on ? 3 : 1), 1, line);
          });
          if (e.domain === 'light') {
            b.element(10 + idx, 8, yy + 38, 100, 24, function () {
              b.textScaled(1, 12, yy + 42, 'LEFT', slate.PAL(1), 1, 'Dim');
            });
            b.element(20 + idx, 120, yy + 38, 100, 24, function () {
              b.textScaled(1, 124, yy + 42, 'LEFT', slate.PAL(1), 1, 'Bright');
            });
          }
        })(i, y);
        y += entities[i].domain === 'light' ? 68 : 44;
      }
      if (entities.length === 0) {
        b.textScaled(1, 120, 100, 'CENTER', slate.PAL(2), 1, 'No entities');
      }
      b.commit();
    });
  }

  function screen() {
    if (mode === 'loading') return statusScreen('Home', 'Fetching...');
    if (mode === 'error') return statusScreen('Home failed', detail);
    return face();
  }

  function push() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    detail = '';
    return push().concat([slate.home.refresh()]);
  };

  global.onBlur = function () {
    return [slate.home.stop()];
  };

  global.onInput = function (ev) {
    if (ev && ev.op === 0x06) {
      return [
        slate.home.stop(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    if (!ev || ev.type !== 'tap') return [{ type: 'inputUnhandled' }];
    var id = ev.elemId | 0;
    if (id >= 1 && id <= 4) {
      var e = entities[id - 1];
      if (!e) return [{ type: 'inputUnhandled' }];
      return [slate.home.toggle(e.id)];
    }
    if (id >= 10 && id < 14) {
      var e2 = entities[id - 10];
      if (!e2) return [{ type: 'inputUnhandled' }];
      var br = (e2.brightness || 128) - 32;
      return [slate.home.set(e2.id, { brightness: br < 0 ? 0 : br })];
    }
    if (id >= 20 && id < 24) {
      var e3 = entities[id - 20];
      if (!e3) return [{ type: 'inputUnhandled' }];
      var br2 = (e3.brightness || 128) + 32;
      return [slate.home.set(e3.id, { brightness: br2 > 255 ? 255 : br2 })];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'home') return [];
    var o;
    try { o = typeof data === 'string' ? JSON.parse(data) : data; } catch (e) { return []; }
    if (!o || !o.type) return [];
    if (o.type === 'status') {
      if (o.state === 'loading') {
        mode = 'loading';
      } else {
        mode = 'error';
        detail = o.detail || o.state || '';
      }
      return push();
    }
    if (o.type === 'states') {
      mode = 'ok';
      entities = o.entities || [];
      return push();
    }
    return [];
  };
})(this);
