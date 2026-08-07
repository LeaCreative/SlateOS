/**
 * Navigation — turn-by-turn directions on the watch.
 *
 * The host pushes ONE maneuver event per change, never a periodic refresh:
 * OsmAnd (or the demo injector) -> NavAdapter -> onEvent('nav', json) -> one
 * display list. That is what keeps a nav app affordable on this link.
 *
 * Draws: maneuver glyph, distance to the turn, street name (truncated to 18
 *        characters), a progress arc, and an ETA line. A status line appears
 *        underneath only when GPS is lost or the phone has gone. Font 1 (5x7)
 *        scaled — the 3x5 base font carries the whole ASCII set but is not
 *        legible at arm's length on a 240x240 panel.
 * Does:  onFocus subscribes to the nav adapter; onBlur and BACK (0x06) or a
 *        left-to-right swipe (0x03) unsubscribe and relinquish. A double-tap
 *        or long-press injects a demo maneuver, and a slide-down injects the
 *        lost-GPS state, so the screen can be exercised with no OsmAnd
 *        running. Buzzes the watch motor once when the turn actually changes
 *        — never on a repeat of the same turn, and never while GPS is lost.
 *        b.retain(120) keeps the last screen on the watch for 120 s so a
 *        dropped link leaves the turn visible rather than a blank face.
 * Perms: navigation — slate.nav.subscribe/unsubscribe/demo are gated on it.
 *        storage — reads the units setting; §5.2 requires the storage
 *        permission to read settings. Nothing is written.
 *        Like Camera, navigation is in PermissionPolicy.THIRD_PARTY_BLOCKED,
 *        so this only runs with the permission because it is bundled Official.
 * Settings: units (metric | imperial, default metric) picks m/km or yd/mi.
 *        Read at focus, so a change applies next time the app is opened.
 * Budget: 101 B waiting, 122 B worst case (18-char street, ETA and a status
 *         line), 6-7 drawing ops — measured. No loops; the street is cut with
 *         substring rather than walked.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var STREET_MAX_CHARS = 18;
  var METRES_PER_MILE = 1609;
  var YARDS_PER_METRE = 1.09361;
  var units = 'metric';

  var last = {
    turn: 'none',
    distanceM: 0,
    street: 'Waiting for route',
    progressPct: 0,
    etaEpochSec: 0,
    status: 'ok'
  };

  /**
   * Configured units, defended.
   *
   * Settings arrive as ordinary store entries and this script could overwrite
   * one, so anything but the two known values falls back to metric
   * (docs/subapp-rules.md §5.2).
   */
  function configuredUnits() {
    var raw = slate.store.get('units');
    return raw === 'imperial' ? 'imperial' : 'metric';
  }

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
    if (units === 'imperial') {
      if (m >= METRES_PER_MILE) return ((m / METRES_PER_MILE).toFixed(1)) + ' mi';
      return ((m * YARDS_PER_METRE) | 0) + ' yd';
    }
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
      // Maneuver "icon" as a large glyph (ICON atlas ids are 0 until assets
      // land). Font 1 (5x7) scaled throughout — see the header note on font 0.
      b.textScaled(1, 120, 14, 'CENTER', slate.PAL(2), 5, glyph);
      b.textScaled(1, 120, 62, 'CENTER', slate.PAL(1), 3, fmtDist(last.distanceM));
      b.textScaled(1, 120, 96, 'CENTER', slate.PAL(1), 2,
        String(last.street).substring(0, STREET_MAX_CHARS));
      b.progressArc(120, 168, 36, last.progressPct | 0, slate.PAL(2), slate.PAL(0), 4);
      b.textScaled(1, 120, 204, 'CENTER', slate.PAL(1), 2, 'ETA ' + fmtEta(last.etaEpochSec));
      if (statusLine) {
        b.textScaled(1, 120, 224, 'CENTER', slate.PAL(3), 2, statusLine);
      }
      b.retain(120);
      b.commit();
    });
  }

  global.onFocus = function () {
    // Settings are read here and only here (§5.2), so a change takes effect
    // the next time the app is opened rather than mid-route.
    units = configuredUnits();
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
