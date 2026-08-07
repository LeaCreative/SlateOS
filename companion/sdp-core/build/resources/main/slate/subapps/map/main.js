/**
 * Local Map — a north-up vector map of where you are.
 *
 * Draws: **almost nothing.** The map itself is rendered by the companion and
 *        pushed under this app's focus, the same privileged path the camera
 *        preview uses. This script draws only the screens that exist when
 *        there is no map: locating, loading, rate-limited, blocked, error.
 * Does:  onFocus subscribes to the map adapter at the configured radius. BACK
 *        (0x06) and onBlur unsubscribe, which stops the GPS and the fetching.
 *        Status arrives as onEvent('map', json); a state of 'ok' means the
 *        host has just drawn the map, so this script deliberately pushes
 *        NOTHING in response — anything it drew would paint over the map.
 * Perms: location — the map is built from the user's position, so slate.map is
 *        gated on exactly the same permission as reading it directly. Like
 *        Camera and Navigation it is in THIRD_PARTY_BLOCKED, so a downloaded
 *        app needs an explicit user grant; this one is bundled at Official
 *        trust.
 *        storage — reads the radiusM setting (§5.2 requires it). Never writes.
 * Settings: radiusM (100-2000 m, default 400) is how far the map reaches from
 *        the centre of the screen to its edge. Read at focus, so a change
 *        applies next time the app is opened. **At 150 m or less the host also
 *        fetches building outlines**; above that it does not ask for them at
 *        all, because a town centre holds more than the display list can carry
 *        and the response is six times larger (see OverpassClient).
 * Budget: 70 B worst case, 3 drawing ops — measured, and that covers only the
 *        status screens, which is all this script ever draws. The map lists
 *        the host pushes are budgeted separately at 2048 B inside MapRenderer,
 *        which drops ways by importance rather than ever exceeding it. Real
 *        OSM data around Victoria renders at 217-2030 B depending on radius.
 *
 * **There is no refresh button, and there cannot be one.** slate.map exposes
 * only subscribe and unsubscribe: the companion decides when to redraw (on
 * movement) and when to refetch (on leaving the cached area). That is what
 * keeps a downloaded script from hammering a free public API, and it is why
 * this app declares "refreshPolicy": "manual" — so the compositor does not
 * repaint it over the host's map either.
 *
 * North is always up. There is no rotation term anywhere in the projection, so
 * that is structural rather than a setting this app could get wrong.
 *
 * Downloaded JavaScript only — never dex/JAR/.so. The map is data the host
 * computed; the watch never executes anything it receives over BLE.
 */
(function (global) {
  'use strict';

  var DEFAULT_RADIUS_M = 400;
  var MIN_RADIUS_M = 100;
  var MAX_RADIUS_M = 2000;

  var state = 'locating';
  var detail = '';
  // True once the host has drawn a map. From then on this script stays off the
  // screen until something takes the map away again.
  var mapOnScreen = false;

  /**
   * Configured radius, defended.
   *
   * Settings arrive as ordinary store entries and this script could overwrite
   * one, so anything outside the declared range falls back to the default
   * (docs/subapp-rules.md §5.2). The host clamps it again regardless.
   */
  function configuredRadius() {
    var n = parseInt(slate.store.get('radiusM'), 10);
    if (isNaN(n) || n < MIN_RADIUS_M || n > MAX_RADIUS_M) return DEFAULT_RADIUS_M;
    return n;
  }

  function headline() {
    if (state === 'locating') return 'Finding you';
    if (state === 'loading') return 'Loading map';
    if (state === 'waiting') return 'Map is busy';
    if (state === 'blocked') return 'No location';
    if (state === 'error') return 'Map failed';
    return 'Map';
  }

  /** One line saying what the user can actually do about it. */
  function subtitle() {
    if (state === 'locating') return 'Waiting for GPS';
    if (state === 'loading') return 'Fetching from OSM';
    if (state === 'waiting') return 'Retrying shortly';
    if (state === 'blocked') {
      if (detail === 'denied') return 'Grant location in Slate';
      if (detail === 'disabled') return 'Turn on phone location';
      return 'No position available';
    }
    if (state === 'error') return detail ? String(detail).substring(0, 20) : 'Check the log';
    return '';
  }

  /** Status-only screen. Never drawn while the host owns the display. */
  function statusScreen() {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0xfd20);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 92, 'CENTER',
        state === 'blocked' || state === 'error' ? slate.PAL(2) : slate.PAL(1),
        2, headline());
      b.textScaled(1, 120, 124, 'CENTER', slate.PAL(1), 1, subtitle());
      b.commit();
    });
  }

  global.onFocus = function () {
    state = 'locating';
    detail = '';
    mapOnScreen = false;
    var r = configuredRadius();
    slate.log('info', 'map: subscribe radius=' + r + 'm');
    return [
      slate.map.subscribe({ radiusM: r }),
      { type: 'pushDisplayList', displayListBase64: statusScreen() }
    ];
  };

  /**
   * Only ever reached by an explicit request, since refreshPolicy is manual.
   * Redrawing the status screen over a live map would be a regression the user
   * sees as the map flickering away, so this returns nothing once a map is up.
   */
  global.render = function () {
    if (mapOnScreen) return [];
    return statusScreen();
  };

  global.onBlur = function () {
    // Unsubscribing here is what stops the GPS and the OSM fetching. Leaving it
    // running behind a screen nobody is looking at would drain the phone.
    return [slate.map.unsubscribe()];
  };

  global.onInput = function (ev) {
    if (ev.op === 0x06) {
      return [
        slate.map.unsubscribe(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }
    // Every other gesture is deliberately unhandled. There is no pan, no zoom
    // and no refresh: the map follows the phone, and the radius is a setting.
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'map') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type !== 'status') return [];

    if (payload.state === 'ok') {
      // The host has just pushed the map. Draw nothing, or it lands on top.
      mapOnScreen = true;
      state = 'ok';
      slate.log('info', 'map: ' + payload.ways + ' ways, ' + payload.bytes + ' B, ' +
        payload.scaleM + 'm scale' +
        (payload.dropped ? ', ' + payload.dropped + ' dropped' : ''));
      return [];
    }

    state = payload.state || state;
    detail = payload.detail || '';
    if (mapOnScreen) {
      // A map is still on screen and this is only a transient hiccup — a
      // deferred refetch, say. Replacing a usable map with "Map is busy" would
      // be strictly worse than leaving the slightly stale map alone.
      if (state === 'loading' || state === 'waiting') return [];
      mapOnScreen = false;
    }
    return [{ type: 'pushDisplayList', displayListBase64: statusScreen() }];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
