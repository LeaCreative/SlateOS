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
