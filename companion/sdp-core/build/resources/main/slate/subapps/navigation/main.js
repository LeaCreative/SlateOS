/**
 * Slate Navigation — flagship turn-by-turn sub-app (downloaded JS only).
 *
 * Host pushes ONE maneuver event per change (never a periodic refresh).
 * OsmAnd (or demo) → NavAdapter → onEvent('nav', json) → single display list.
 *
 * Behaviour:
 * - status=lost_gps: keep last turn, show "GPS lost", no haptic spam
 * - status=disconnected: show "Phone disconnected"; watch keeps last DL retained
 * - Back / edge swipe: unsubscribe + relinquish
 */
(function (global) {
  'use strict';

  var last = {
    turn: 'none',
    distanceM: 0,
    street: 'Waiting for route',
    progressPct: 0,
    etaEpochSec: 0,
    status: 'ok'
  };

  function turnGlyph(turn) {
    switch (turn) {
      case 'left':
      case 'slight_left': return '<';
      case 'right':
      case 'slight_right': return '>';
      case 'u_turn': return 'U';
      case 'arrive': return '*';
      case 'straight': return '^';
      default: return '-';
    }
  }

  function fmtDist(m) {
    m = m | 0;
    if (m >= 1000) return ((m / 1000).toFixed(1)) + ' km';
    return m + ' m';
  }

  function fmtEta(epoch) {
    epoch = epoch | 0;
    if (!epoch) return '--:--';
    var d = new Date(epoch * 1000);
    var h = d.getHours();
    var mi = d.getMinutes();
    return (h < 10 ? '0' : '') + h + ':' + (mi < 10 ? '0' : '') + mi;
  }

  function face() {
    var statusLine = '';
    if (last.status === 'lost_gps') statusLine = 'GPS lost';
    else if (last.status === 'disconnected') statusLine = 'Phone gone';
    var glyph = turnGlyph(last.turn);
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x07ff);
      b.palette(3, 0xfd20);
      b.clear(slate.PAL(0));
      // Maneuver "icon" as large glyph (ICON atlas ids are 0 until assets land).
      b.text(0, 120, 36, 'CENTER', slate.PAL(2), glyph);
      b.text(0, 120, 78, 'CENTER', slate.PAL(1), fmtDist(last.distanceM));
      b.text(0, 120, 110, 'CENTER', slate.PAL(1), String(last.street).substring(0, 18));
      b.progressArc(120, 168, 36, last.progressPct | 0, slate.PAL(2), slate.PAL(0), 4);
      b.text(0, 120, 200, 'CENTER', slate.PAL(1), 'ETA ' + fmtEta(last.etaEpochSec));
      if (statusLine) {
        b.text(0, 120, 220, 'CENTER', slate.PAL(3), statusLine);
      }
      b.retain(120);
      b.commit();
    });
  }

  global.onFocus = function () {
    return [slate.nav.subscribe(), { type: 'pushDisplayList', displayListBase64: face() }];
  };

  global.render = function () {
    return face();
  };

  global.onBlur = function () {
    return [slate.nav.unsubscribe()];
  };

  global.onInput = function (ev) {
    if (ev.op === 0x06 || ev.op === 0x03) {
      return [slate.nav.unsubscribe(), { type: 'relinquishFocus' }, { type: 'inputHandled' }];
    }
    // Long-press / double-tap demo injection when OsmAnd is absent.
    if (ev.op === 0x0b || ev.op === 0x0c) {
      return [slate.nav.demo('left'), { type: 'inputHandled' }];
    }
    // Slide-down demo: GPS lost behaviour.
    if (ev.op === 0x01) {
      return [slate.nav.demo('lost_gps'), { type: 'inputHandled' }];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'nav') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type !== 'maneuver') return [];
    var prevTurn = last.turn;
    last.turn = payload.turn || last.turn;
    last.distanceM = payload.distanceM | 0;
    last.street = payload.street || last.street;
    last.progressPct = payload.progressPct | 0;
    last.etaEpochSec = payload.etaEpochSec | 0;
    last.status = payload.status || 'ok';
    var out = [
      slate.invalidate(),
      { type: 'pushDisplayList', displayListBase64: face() }
    ];
    if (last.status === 'ok' && last.turn !== prevTurn && last.turn !== 'none') {
      out.unshift(slate.haptic(1));
    }
    return out;
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
