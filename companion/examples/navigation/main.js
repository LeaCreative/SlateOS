/**
 * Navigation — OsmAnd turn-by-turn on the watch.
 *
 * OsmAnd AIDL (and OsmAnd's nav notification) push maneuvers into the host.
 * Turn arrows are relative to direction of travel (route course), never the
 * phone's compass orientation.
 *
 * Draws: large turn glyph (up-arrow rects for straight; 5x7 `{` tick is the
 *        built-in checkmark on arrive), distance to next turn, street,
 *        distance to destination; a dedicated Destination reached face.
 * Does:  onFocus subscribes; onBlur / BACK / swipe-right leave. Long-press or
 *        double-tap injects a demo left turn (no OsmAnd needed). Slide-down
 *        demos lost GPS. Slide-up demos destination reached. Haptic once when
 *        the turn token changes.
 * Perms: navigation, storage (units setting).
 * Settings: units (metric | imperial).
 * Budget: text + a few rects/lines; no loops.
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
    street: 'Waiting for OsmAnd',
    progressPct: 0,
    etaEpochSec: 0,
    destinationDistanceM: 0,
    roundaboutExit: 0,
    status: 'ok'
  };

  function configuredUnits() {
    var raw = slate.store.get('units');
    return raw === 'imperial' ? 'imperial' : 'metric';
  }

  /**
   * Arrow glyph for a travel-relative turn. Not a compass — OsmAnd's turn is
   * already in the vehicle's direction of motion.
   *
   * 5x7 `{` is the tick pictogram (not a brace). `!` is a vertical bar + dot
   * (off-route). `-` is a horizontal dash (waiting). Straight is drawn with
   * rects because the caret `^` sits in the top 3 rows of the cell and reads
   * as a short cyan dash on the wrist.
   */
  function turnGlyph(turn) {
    switch (turn) {
      case 'left':
      case 'sharp_left':
      case 'keep_left':
        return '<';
      case 'slight_left':
        return '<<';
      case 'right':
      case 'sharp_right':
      case 'keep_right':
        return '>';
      case 'slight_right':
        return '>>';
      case 'u_turn':
        return 'U';
      case 'arrive':
        return '{';
      case 'roundabout':
        return 'O';
      case 'off_route':
        return '!';
      default:
        return '-';
    }
  }

  function turnLabel(turn) {
    switch (turn) {
      case 'left': return 'Left';
      case 'right': return 'Right';
      case 'slight_left': return 'Slight L';
      case 'slight_right': return 'Slight R';
      case 'sharp_left': return 'Sharp L';
      case 'sharp_right': return 'Sharp R';
      case 'keep_left': return 'Keep L';
      case 'keep_right': return 'Keep R';
      case 'u_turn': return 'U-turn';
      case 'arrive': return 'Reached';
      case 'roundabout':
        return last.roundaboutExit ? ('Exit ' + last.roundaboutExit) : 'Roundabt';
      case 'off_route': return 'Off route';
      case 'straight': return 'Straight';
      default: return 'Nav';
    }
  }

  function fmtDist(m) {
    m = m | 0;
    if (m <= 0) return '--';
    if (units === 'imperial') {
      if (m >= METRES_PER_MILE) return ((m / METRES_PER_MILE).toFixed(1)) + ' mi';
      return ((m * YARDS_PER_METRE) | 0) + ' yd';
    }
    if (m >= 1000) return ((m / 1000).toFixed(1)) + ' km';
    return m + ' m';
  }

  /** Continue / go-ahead: shaft + chevron, travel-relative (up the screen). */
  function drawStraight(b, c) {
    b.line(120, 44, 98, 68, c, 5);
    b.line(120, 44, 142, 68, c, 5);
    b.rect(114, 56, 12, 38, c, slate.FILL);
  }

  function face() {
    var statusLine = '';
    if (last.status === 'lost_gps') statusLine = 'GPS lost';
    else if (last.status === 'disconnected') statusLine = 'Phone gone';
    var arrived = last.turn === 'arrive';
    var glyph = turnGlyph(last.turn);
    var turnDist = fmtDist(last.distanceM);
    var destDist = fmtDist(last.destinationDistanceM);
    var street = String(last.street || '').substring(0, STREET_MAX_CHARS);
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x07ff);
      b.palette(3, 0xfd20);
      b.palette(4, 0x8410);
      b.palette(5, 0x07e0);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 8, 'CENTER', slate.PAL(4), 1, turnLabel(last.turn));
      if (last.turn === 'straight') {
        drawStraight(b, slate.PAL(2));
      } else {
        b.textScaled(1, 120, 48, 'CENTER', slate.PAL(arrived ? 5 : 2), 5, glyph);
      }
      if (arrived) {
        b.textScaled(1, 120, 120, 'CENTER', slate.PAL(1), 2, 'Destination');
        b.textScaled(1, 120, 148, 'CENTER', slate.PAL(1), 2, 'reached');
        if (street) {
          b.textScaled(1, 120, 188, 'CENTER', slate.PAL(4), 1, street);
        }
      } else {
        b.textScaled(1, 120, 100, 'CENTER', slate.PAL(1), 3, turnDist);
        b.textScaled(1, 120, 132, 'CENTER', slate.PAL(4), 1, 'to turn');
        if (street) {
          b.textScaled(1, 120, 156, 'CENTER', slate.PAL(1), 1, street);
        }
        b.textScaled(1, 120, 188, 'CENTER', slate.PAL(1), 2, destDist);
        b.textScaled(1, 120, 214, 'CENTER', slate.PAL(4), 1, 'to destination');
      }
      if (statusLine) {
        b.textScaled(1, 120, 232, 'CENTER', slate.PAL(3), 1, statusLine);
      }
      b.retain(120);
      b.commit();
    });
  }

  global.onFocus = function () {
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
    if (ev.op === 0x0b || ev.op === 0x0c) {
      return [slate.nav.demo('left'), { type: 'inputHandled' }];
    }
    if (ev.op === 0x01) {
      return [slate.nav.demo('lost_gps'), { type: 'inputHandled' }];
    }
    if (ev.op === 0x02) {
      return [slate.nav.demo('arrive'), { type: 'inputHandled' }];
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
    last.street = (typeof payload.street === 'string') ? payload.street : last.street;
    last.progressPct = payload.progressPct | 0;
    last.etaEpochSec = payload.etaEpochSec | 0;
    last.destinationDistanceM = payload.destinationDistanceM | 0;
    last.roundaboutExit = payload.roundaboutExit | 0;
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
