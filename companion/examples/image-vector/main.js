/**
 * Image Vector -- full-screen vector approximation of the Slate logo.
 *
 * Companion to `examples/image` (63x63 RGB332 PATCH). Same source art, the
 * opposite encoding: geometry instead of pixels, which buys the full 240x240
 * face for a fraction of the bytes.
 *
 * Draws: black CLEAR, then 23 filled circles painted in order (painter's
 *        algorithm) — nested teal disks, black carves for the open quadrant
 *        and the scalloped inner edge, then a bright cyan crest.
 * Does:  nothing but draw. onFocus logs and pushes the screen, render()
 *        repeats it, BACK (0x06) relinquishes focus. No adapters, no timers,
 *        no persistence.
 * Perms: none, and none are needed — slate.ui and slate.log are not gated.
 * Settings: none. The disc table is artwork, not a preference.
 * Budget: 174 B display list, 33 ops, 24 of them drawing — measured. Tiny on
 *        the wire: 23x smaller than examples/image, and nowhere near either
 *        the 4096 B cap or the 2048 B practical limit.
 *
 *        **The cost of this app is render time, not bytes**, and the two are
 *        independent (§2.1). The renderer replays the whole list once per
 *        tile, 30 times, so 23 filled discs are walked 690 times per screen —
 *        7 of them with r >= 78 and the largest at r=120, a disc wider than
 *        the panel. This is the most expensive screen in examples/ by a
 *        distance, and deliberately so: it is the stress case.
 *
 *        It is also the app that **reset the watch** on 6 Aug 2026 (N-44 /
 *        N-45) and the reason docs/subapp-rules.md exists. Filled circles were
 *        then rad+1 concentric Bresenham outlines — about 58,000 plotted
 *        points for one r=120 disc — and the renderer never petted the
 *        watchdog, so the app task ran past the ~7 s bootloader dog. Both are
 *        fixed in firmware: discs are scanline spans (O(r), not O(r^2)) and
 *        the pet runs inside the bounded 30-iteration tile loop. The art was
 *        not the bug and has not been changed.
 *
 *        Not measured: render milliseconds on hardware, before or after the
 *        span-fill fix. The op counts and radii above are counted from the
 *        emitted list; the cost claim is reasoned from them, not timed.
 *
 *        The DISKS table is module-scope const on purpose — §2.2 SHOULD:
 *        precompute static geometry once rather than per render. The only loop
 *        in the app is bounded by DISKS.length.
 *
 * Install: zip manifest.json + main.js and open on the phone (SideloadActivity).
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  // RGB565 palette sampled from logo.png (composited on black).
  var PAL = {
    black: 0x0000,
    outer: 0x6678,  // mint
    band2: 0x2dd6,
    band3: 0x0d35,
    band4: 0x0473,
    band5: 0x0390,
    head: 0x0dfa,   // bright cyan crest
    shadow: 0x0187
  };

  // Circle ops: [cx, cy, r, paletteKey]. Painted in order (painter's algorithm).
  var DISKS = [
    [121, 115, 118, 'shadow'],
    [120, 112, 118, 'outer'],
    [120, 112, 108, 'band2'],
    [120, 112, 98, 'band3'],
    [120, 112, 88, 'band4'],
    [120, 112, 78, 'band5'],
    // Hollow the centre, then open the bottom-right into a C / wave.
    [120, 112, 68, 'black'],
    [200, 175, 120, 'black'],
    // Scalloped inner edge.
    [70, 200, 40, 'black'],
    [92, 192, 38, 'black'],
    [112, 178, 40, 'black'],
    [130, 162, 36, 'black'],
    [146, 146, 32, 'black'],
    [160, 132, 28, 'black'],
    [172, 120, 22, 'black'],
    [85, 170, 30, 'black'],
    [108, 155, 28, 'black'],
    [128, 140, 26, 'black'],
    // Crest on the right.
    [170, 122, 40, 'head'],
    [200, 168, 58, 'black'],
    [158, 114, 14, 'band3'],
    // Sharp outer tip, bottom-left.
    [48, 170, 7, 'outer'],
    [42, 182, 14, 'black']
  ];

  function face() {
    return slate.ui.displayList(function (b) {
      b.palette(0, PAL.black);
      b.palette(1, PAL.outer);
      b.palette(2, PAL.band2);
      b.palette(3, PAL.band3);
      b.palette(4, PAL.band4);
      b.palette(5, PAL.band5);
      b.palette(6, PAL.head);
      b.palette(7, PAL.shadow);
      b.clear(slate.PAL(0));

      var keyToIdx = {
        black: 0, outer: 1, band2: 2, band3: 3,
        band4: 4, band5: 5, head: 6, shadow: 7
      };
      for (var i = 0; i < DISKS.length; i++) {
        var d = DISKS[i];
        b.circle(d[0], d[1], d[2], slate.PAL(keyToIdx[d[3]]), slate.FILL);
      }
      b.commit();
    });
  }

  global.onFocus = function () {
    slate.log('info', 'image-vector: full-screen circle approx');
    return face();
  };

  global.render = function () {
    return face();
  };

  global.onBlur = function () {
    return [];
  };

  global.onInput = function (ev) {
    if (ev.op === 0x06) {
      return [{ type: 'relinquishFocus' }, { type: 'inputHandled' }];
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function () {
    return [];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
