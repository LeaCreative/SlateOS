/**
 * Host bridge: maps lifecycle exports to dispatch(msg) JSON protocol.
 * Sub-apps export onFocus/render/onInput/onEvent/onBlur (or a single dispatch).
 */
(function (global) {
  'use strict';

  function outboundPush(b64) {
    return [{ type: 'pushDisplayList', displayListBase64: b64 }];
  }

  function callExport(name) {
    var fn = global[name] || (global.exports && global.exports[name]);
    if (typeof fn !== 'function') return null;
    var args = Array.prototype.slice.call(arguments, 1);
    return fn.apply(null, args);
  }

  global.__slate_dispatch = function (msgJson) {
    var msg = (typeof msgJson === 'string') ? JSON.parse(msgJson) : msgJson;
    var out = [];

    function merge(r) {
      if (r == null) return;
      if (Object.prototype.toString.call(r) === '[object Array]') {
        for (var i = 0; i < r.length; i++) out.push(r[i]);
      } else if (typeof r === 'string') {
        // render returned base64 display list
        out.push({ type: 'pushDisplayList', displayListBase64: r });
      } else if (typeof r === 'object') {
        out.push(r);
      }
    }

    switch (msg.type) {
      case 'focus':
        merge(callExport('onFocus', msg));
        merge(callExport('render', msg));
        break;
      case 'render':
        merge(callExport('render', msg));
        break;
      case 'blur':
        merge(callExport('onBlur', msg));
        break;
      case 'input':
        merge(callExport('onInput', msg));
        if (out.length === 0) out.push({ type: 'inputUnhandled' });
        break;
      case 'event':
        merge(callExport('onEvent', msg.source, msg.data));
        break;
      case 'create':
      case 'start':
      case 'stop':
      case 'destroy':
        break;
      default:
        if (typeof global.dispatch === 'function') {
          merge(global.dispatch(msg));
        }
        break;
    }
    return JSON.stringify(out);
  };

  // Default binding stubs — host overwrites permission-gated ones after load.
  var slate = global.slate || {};
  slate.invalidate = function () {
    return { type: 'invalidate' };
  };
  slate.log = function () {
    var level = 'info';
    var message = '';
    if (arguments.length === 1) { message = String(arguments[0]); }
    else { level = String(arguments[0]); message = String(arguments[1]); }
    return { type: 'log', level: level, message: message };
  };
  slate.haptic = function (pattern) {
    return { type: 'adapter', adapter: 'haptic', command: 'pulse', payload: JSON.stringify({ pattern: pattern || 0 }) };
  };
  /**
   * Vibrate the PHONE. slate.haptic() buzzes the watch motor; this reaches
   * back to the handset. Permission-gated on "vibrate".
   */
  slate.phone = {
    vibrate: function (ms) {
      var d = ms | 0;
      if (d < 0) d = 0;
      if (d > 2000) d = 2000;
      return { type: 'adapter', adapter: 'phone', command: 'vibrate',
        payload: JSON.stringify({ ms: d }) };
    }
  };
  slate.timer = {
    set: function (id, ms) {
      return { type: 'adapter', adapter: 'timer', command: 'set',
        payload: JSON.stringify({ id: String(id), intervalMs: ms | 0 }) };
    },
    clear: function (id) {
      return { type: 'adapter', adapter: 'timer', command: 'clear',
        payload: JSON.stringify({ id: String(id) }) };
    }
  };
  slate.store = {
    get: function (k) { return (global.__slate_store && global.__slate_store[k]) || null; },
    set: function (k, v) {
      return { type: 'adapter', adapter: 'store', command: 'set',
        payload: JSON.stringify({ key: String(k), value: String(v) }) };
    }
  };
  /**
   * Location — the phone's position, gated on the "location" permission.
   *
   * Fixes arrive as onEvent('location', json), never as a return value: the
   * phone may take seconds to get one, and nothing on the compositor path may
   * block waiting (docs/subapp-rules.md §2.3).
   *
   *   { "type": "fix", "lat": …, "lon": …, "accuracyM": …, "altitudeM": …,
   *     "speedMps": …, "bearingDeg": …, "timeEpochSec": …, "provider": "gps" }
   *   { "type": "status", "state": "searching" | "denied" | "disabled" |
   *     "unavailable" }
   *
   * A sub-app MUST handle the status states. "denied" means the user has not
   * given the companion the Android runtime permission and no fix will ever
   * arrive; "disabled" means location is switched off phone-wide. Neither is
   * recoverable from inside the script, and both are common.
   *
   * minIntervalMs is clamped host-side to >= 1000: a sub-app must not be able
   * to hold the GPS on at an arbitrary rate.
   */
  slate.location = {
    subscribe: function (opts) {
      opts = opts || {};
      return { type: 'adapter', adapter: 'location', command: 'subscribe',
        payload: JSON.stringify({
          minIntervalMs: (opts.minIntervalMs == null ? 5000 : opts.minIntervalMs) | 0,
          minDistanceM: (opts.minDistanceM == null ? 0 : opts.minDistanceM) | 0
        }) };
    },
    unsubscribe: function () {
      return { type: 'adapter', adapter: 'location', command: 'unsubscribe', payload: '{}' };
    },
    /**
     * One fix, then stop. Answers from the last known position when there is
     * one recent enough, so a screen can show something immediately rather
     * than sitting on "searching" while the GPS warms up.
     */
    request: function () {
      return { type: 'adapter', adapter: 'location', command: 'request', payload: '{}' };
    }
  };
  /**
   * Map — a north-up vector map of the surroundings, drawn by the host.
   *
   * This is a *thin controller*, like slate.camera: OSM data never enters this
   * isolate and neither does the map's display list. The host fetches, projects
   * and renders, then pushes the screen under this app's focus. A script could
   * not do this work anyway — it has no network binding, and projecting and
   * simplifying a few hundred ways would blow the 500 ms eval deadline.
   *
   * **There is deliberately no refresh command.** The companion decides when to
   * redraw (on movement) and when to refetch (on leaving the cached area), so a
   * sub-app cannot poll, cannot hammer a free public API, and cannot get the
   * refresh rate wrong. Subscribe once, then only receive.
   *
   * Status arrives as onEvent('map', json):
   *
   *   { "type": "status", "state": "ok", "ways": …, "dropped": …,
   *     "bytes": …, "scaleM": …, "ageSec": … }
   *   { "type": "status", "state": "locating" | "loading" | "waiting" }
   *   { "type": "status", "state": "blocked", "detail": "denied" | … }
   *   { "type": "status", "state": "error", "detail": "…" }
   *
   * "ok" means the host has just put a map on the screen — the sub-app should
   * draw nothing at that point or it will paint over it. Declare
   * "refreshPolicy": "manual" so the compositor does not repaint you either.
   */
  slate.map = {
    subscribe: function (opts) {
      opts = opts || {};
      return { type: 'adapter', adapter: 'map', command: 'subscribe',
        payload: JSON.stringify({
          radiusM: (opts.radiusM == null ? 400 : opts.radiusM) | 0
        }) };
    },
    unsubscribe: function () {
      return { type: 'adapter', adapter: 'map', command: 'unsubscribe', payload: '{}' };
    }
  };
  /**
   * News — host fetches RSS/Atom; titles and article pages arrive as
   * onEvent('news', json). Feed URL comes from the feedUrl setting (storage).
   *
   *   slate.news.list(feedUrl)
   *   slate.news.page(id, pageIndex)
   *   slate.news.stop()
   *
   * Events:
   *   { "type": "status", "state": "loading"|"empty"|"need_url"|"error", "detail"? }
   *   { "type": "list", "items": [{ "id", "title" }, …] }  // up to 8
   *   { "type": "page", "id", "page", "pageCount", "text" }
   */
  slate.news = {
    list: function (feedUrl) {
      return { type: 'adapter', adapter: 'news', command: 'list',
        payload: JSON.stringify({ feedUrl: String(feedUrl || '') }) };
    },
    page: function (id, page) {
      return { type: 'adapter', adapter: 'news', command: 'page',
        payload: JSON.stringify({
          id: String(id || ''),
          page: (page == null ? 0 : page) | 0
        }) };
    },
    stop: function () {
      return { type: 'adapter', adapter: 'news', command: 'stop', payload: '{}' };
    }
  };
  /**
   * Media — now-playing + transport. Requires companion Notification Listener.
   * Events: onEvent('media', json)
   *   { type: 'nowPlaying', playing, title, artist, album, app }
   *   { type: 'status', state: 'idle'|'denied'|'error', detail? }
   */
  slate.media = {
    subscribe: function () {
      return { type: 'adapter', adapter: 'media', command: 'subscribe', payload: '{}' };
    },
    unsubscribe: function () {
      return { type: 'adapter', adapter: 'media', command: 'unsubscribe', payload: '{}' };
    },
    play: function () {
      return { type: 'adapter', adapter: 'media', command: 'play', payload: '{}' };
    },
    pause: function () {
      return { type: 'adapter', adapter: 'media', command: 'pause', payload: '{}' };
    },
    next: function () {
      return { type: 'adapter', adapter: 'media', command: 'next', payload: '{}' };
    },
    previous: function () {
      return { type: 'adapter', adapter: 'media', command: 'previous', payload: '{}' };
    }
  };
  /**
   * HTTP — host GET/POST. Manifest must declare permission "http" and
   * http.allowedHosts. Response capped (~64 KiB). Events: onEvent('http', json)
   *   { type: 'response', id, status, body }
   *   { type: 'error', id, detail }
   */
  slate.http = {
    get: function (url, opts) {
      opts = opts || {};
      return { type: 'adapter', adapter: 'http', command: 'get',
        payload: JSON.stringify({
          url: String(url || ''),
          id: String(opts.id == null ? '' : opts.id)
        }) };
    },
    post: function (url, body, opts) {
      opts = opts || {};
      return { type: 'adapter', adapter: 'http', command: 'post',
        payload: JSON.stringify({
          url: String(url || ''),
          body: body == null ? null : String(body),
          id: String(opts.id == null ? '' : opts.id)
        }) };
    },
    cancel: function (id) {
      return { type: 'adapter', adapter: 'http', command: 'cancel',
        payload: JSON.stringify({ id: String(id || '') }) };
    },
    stop: function () {
      return { type: 'adapter', adapter: 'http', command: 'stop', payload: '{}' };
    }
  };
  /**
   * Weather — host Open-Meteo snapshot. Optional lat/lon; else last-known GPS.
   * Events: onEvent('weather', json)
   *   { type: 'snapshot', tempC, weatherCode, label, precipMm, windMps, lat, lon }
   *   { type: 'status', state: 'loading'|'error', detail? }
   */
  slate.weather = {
    fetch: function (opts) {
      opts = opts || {};
      var payload = {};
      if (opts.lat != null) payload.lat = +opts.lat;
      if (opts.lon != null) payload.lon = +opts.lon;
      return { type: 'adapter', adapter: 'weather', command: 'fetch',
        payload: JSON.stringify(payload) };
    },
    stop: function () {
      return { type: 'adapter', adapter: 'weather', command: 'stop', payload: '{}' };
    }
  };
  /** Navigation — host adapter; maneuvers arrive as onEvent('nav', …). */
  slate.nav = {
    subscribe: function () {
      return { type: 'adapter', adapter: 'nav', command: 'subscribe', payload: '{}' };
    },
    unsubscribe: function () {
      return { type: 'adapter', adapter: 'nav', command: 'unsubscribe', payload: '{}' };
    },
    /** Demo/test: ask host to inject a synthetic maneuver (official builds only). */
    demo: function (kind) {
      return { type: 'adapter', adapter: 'nav', command: 'demo',
        payload: JSON.stringify({ kind: String(kind || 'left') }) };
    }
  };
  /**
   * Camera — thin controller API. Preview pixels never enter the isolate;
   * the host pushes PATCH display lists (privileged streaming path).
   */
  slate.camera = {
    start: function (opts) {
      opts = opts || {};
      return { type: 'adapter', adapter: 'camera', command: 'start',
        payload: JSON.stringify({
          slot: opts.slot | 0,
          x: opts.x | 0,
          y: opts.y | 0,
          w: (opts.w == null ? 60 : opts.w) | 0,
          h: (opts.h == null ? 60 : opts.h) | 0
        }) };
    },
    stop: function () {
      return { type: 'adapter', adapter: 'camera', command: 'stop', payload: '{}' };
    },
    capture: function () {
      return { type: 'adapter', adapter: 'camera', command: 'capture', payload: '{}' };
    }
  };
  // Patch format constants for script-built patches (rare; host owns camera frames).
  slate.PATCH_RGB332 = 1;
  slate.PATCH_RAW = 0;
  global.slate = slate;
})(typeof globalThis !== 'undefined' ? globalThis : this);
