/**
 * News JS — RSS/Atom headlines on the watch (downloaded JavaScript).
 *
 * Draws: loading/error screens; a windowed list of up to 8 titles as outline
 *        buttons (4 visible); one screen of article text at a time.
 * Does:  onFocus asks the host (slate.news.list) for the feedUrl setting.
 *        Tap a row → host pages the body; swipe UP/DOWN pages text or scrolls
 *        the list window; BACK (0x06) / left-swipe returns list→launcher or
 *        article→list. Host fetches and parses — no HTTP in the isolate.
 * Perms: news — host RSS adapter. storage — feedUrl setting (§5.2).
 * Setting: feedUrl (string, max 512). Empty → prompt to set it in the repo.
 * Budget: list ~4 outline rows + title; article lines from host pages. No loops
 *        over feed XML in JS.
 *
 * Downloaded JavaScript only — never dex/JAR/.so.
 */
(function (global) {
  'use strict';

  var VISIBLE = 4;
  var ROW_H = 44;
  var ROW_PITCH = 48;
  var ROW_X = 8;
  var ROW_W = 224;
  var ROW_Y0 = 40;
  // Font 1 (5x7) at scale 2 ≈ 12 px/glyph; ~17 fit in the row. ASCII "..."
  // only — U+2026 is not in the watch font, so titles looked clipped with no
  // ellipsis.
  var TITLE_CHARS = 16;

  var mode = 'loading'; // loading | list | article | error | need_url | empty
  var detail = '';
  var items = []; // { id, title }
  var listOffset = 0;
  var articleId = '';
  var articlePage = 0;
  var articlePageCount = 1;
  var articleText = '';

  function feedUrl() {
    var raw = slate.store.get('feedUrl');
    if (raw == null) return '';
    return String(raw).trim();
  }

  /** Fit to n glyphs using ASCII "..." (watch fonts are ASCII-only). */
  function trunc(s, n) {
    s = String(s || '');
    if (s.length <= n) return s;
    if (n <= 3) return s.substring(0, n);
    return s.substring(0, n - 3) + '...';
  }

  function statusScreen(title, sub) {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0xfd20);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 88, 'CENTER', slate.PAL(1), 2, trunc(title, 16));
      if (sub) {
        b.textScaled(1, 120, 128, 'CENTER', slate.PAL(2), 1, trunc(sub, 28));
      }
      b.commit();
    });
  }

  function listScreen() {
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.clear(slate.PAL(0));
      b.textScaled(1, 120, 10, 'CENTER', slate.PAL(1), 2, 'News JS');
      var i;
      for (i = 0; i < VISIBLE; i++) {
        (function (idx, top, hitId) {
          if (idx >= items.length) return;
          var label = trunc(items[idx].title, TITLE_CHARS);
          b.element(hitId, ROW_X, top, ROW_W, ROW_H, function () {
            b.rectRound(ROW_X, top, ROW_W, ROW_H, 8, slate.PAL(1), 1);
            b.textScaled(1, ROW_X + 10, top + 14, 'LEFT', slate.PAL(1), 2, label);
          });
        })(listOffset + i, ROW_Y0 + i * ROW_PITCH, i + 1);
      }
      if (items.length > VISIBLE) {
        b.textScaled(1, 120, 228, 'CENTER', slate.PAL(2), 1,
          (listOffset + 1) + '-' + Math.min(listOffset + VISIBLE, items.length) +
            '/' + items.length);
      }
      b.commit();
    });
  }

  function articleScreen() {
    var meta = (articlePage + 1) + '/' + articlePageCount;
    var lines = String(articleText || '').split('\n');
    return slate.ui.displayList(function (b) {
      b.palette(0, 0x0000);
      b.palette(1, 0xffff);
      b.palette(2, 0x8410);
      b.clear(slate.PAL(0));
      b.textScaled(1, 8, 6, 'LEFT', slate.PAL(2), 1, meta);
      var li;
      for (li = 0; li < lines.length && li < 8; li++) {
        b.textScaled(1, 8, 24 + li * 24, 'LEFT', slate.PAL(1), 2, trunc(lines[li], 16));
      }
      b.commit();
    });
  }

  function screen() {
    if (mode === 'need_url') {
      return statusScreen('Set feed URL', 'Repo > News settings');
    }
    if (mode === 'loading') return statusScreen('Loading', 'Fetching feed');
    if (mode === 'empty') return statusScreen('No items', 'Feed was empty');
    if (mode === 'error') return statusScreen('Feed failed', detail || 'Check URL');
    if (mode === 'article') return articleScreen();
    return listScreen();
  }

  function pushScreen() {
    return [{ type: 'pushDisplayList', displayListBase64: screen() }];
  }

  global.onFocus = function () {
    mode = 'loading';
    detail = '';
    items = [];
    listOffset = 0;
    articleId = '';
    var url = feedUrl();
    slate.log('info', 'news focus feed=' + (url ? url.substring(0, 40) : '(empty)'));
    if (!url) {
      mode = 'need_url';
      return pushScreen();
    }
    return [slate.news.list(url)].concat(pushScreen());
  };

  global.render = function () {
    return screen();
  };

  global.onBlur = function () {
    return [slate.news.stop()];
  };

  global.onInput = function (ev) {
    if (ev.op === 0x06) {
      if (mode === 'article') {
        mode = 'list';
        articleId = '';
        return pushScreen().concat([{ type: 'inputHandled' }]);
      }
      return [
        slate.news.stop(),
        { type: 'relinquishFocus' },
        { type: 'inputHandled' }
      ];
    }

    // Swipe: UP=0 DOWN=1 (LEFT becomes BACK on the host for this app).
    if (ev.op === 0x03) {
      var dir = ev.dir | 0;
      if (mode === 'article') {
        if (dir === 0 && articlePage > 0) {
          return [slate.news.page(articleId, articlePage - 1), { type: 'inputHandled' }];
        }
        if (dir === 1 && articlePage + 1 < articlePageCount) {
          return [slate.news.page(articleId, articlePage + 1), { type: 'inputHandled' }];
        }
        return [{ type: 'inputHandled' }];
      }
      if (mode === 'list' && items.length > VISIBLE) {
        if (dir === 0 && listOffset > 0) {
          listOffset = Math.max(0, listOffset - VISIBLE);
          return pushScreen().concat([{ type: 'inputHandled' }]);
        }
        if (dir === 1 && listOffset + VISIBLE < items.length) {
          listOffset = Math.min(items.length - VISIBLE, listOffset + VISIBLE);
          return pushScreen().concat([{ type: 'inputHandled' }]);
        }
        return [{ type: 'inputHandled' }];
      }
      return [{ type: 'inputUnhandled' }];
    }

    if (mode === 'list' && ev.elemId >= 1 && ev.elemId <= VISIBLE) {
      var idx = listOffset + (ev.elemId - 1);
      if (idx >= 0 && idx < items.length) {
        articleId = items[idx].id;
        articlePage = 0;
        mode = 'article';
        articleText = '...';
        // Article layout with ellipsis — host delivers body on the same input
        // turn (no separate Loading screen that can stick if the event races).
        return [slate.news.page(articleId, 0)].concat(pushScreen()).concat(
          [{ type: 'inputHandled' }]
        );
      }
    }
    return [{ type: 'inputUnhandled' }];
  };

  global.onEvent = function (source, data) {
    if (source !== 'news') return [];
    var payload = typeof data === 'string' ? JSON.parse(data || '{}') : (data || {});
    if (payload.type === 'status') {
      if (payload.state === 'loading') {
        mode = 'loading';
        return pushScreen();
      }
      if (payload.state === 'need_url') {
        mode = 'need_url';
        return pushScreen();
      }
      if (payload.state === 'empty') {
        mode = 'empty';
        return pushScreen();
      }
      if (payload.state === 'error') {
        mode = 'error';
        detail = payload.detail || '';
        return pushScreen();
      }
      return [];
    }
    if (payload.type === 'list') {
      items = payload.items || [];
      listOffset = 0;
      mode = items.length ? 'list' : 'empty';
      return pushScreen();
    }
    if (payload.type === 'page') {
      articleId = payload.id || articleId;
      articlePage = payload.page | 0;
      articlePageCount = Math.max(1, payload.pageCount | 0);
      articleText = payload.text || '';
      mode = 'article';
      return pushScreen();
    }
    return [];
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
