package slate.app.apps

import slate.app.settings.WatchUiTheme
import slate.dsl.DisplayListBuilder
import slate.wire.Style
import slate.wire.rgb

/**
 * Watch navigation chrome shared by the launcher (phone-built lists).
 * Firmware draws the same geometry on Face / Settings / Notifications.
 *
 * Horizontal strip: Settings | Face | Launcher (left → right).
 * Vertical strip: one thin bar per page; current = faceBright, else faceDim.
 */
object NavChrome {
    enum class Section { Settings, Face, Launcher }

    private const val SECTION_Y = 2
    private const val SECTION_H = 3
    private const val SECTION_W = 28
    private const val SECTION_GAP = 8

    private const val PAGE_X = 2
    private const val PAGE_W = 3
    private const val PAGE_H = 14
    private const val PAGE_GAP = 4
    private const val PAGE_BAND_MID = 132

    fun DisplayListBuilder.sectionBars(active: Section) {
        val bright = rgb(WatchUiTheme.faceBright)
        val dim = rgb(WatchUiTheme.faceDim)
        val total = 3 * SECTION_W + 2 * SECTION_GAP
        val x0 = (240 - total) / 2
        for (i in 0 until 3) {
            val on = i == active.ordinal
            rect(
                x0 + i * (SECTION_W + SECTION_GAP),
                SECTION_Y,
                SECTION_W,
                SECTION_H,
                if (on) bright else dim,
                Style.FILL,
            )
        }
    }

    fun DisplayListBuilder.pageBars(pageCount: Int, pageIndex: Int) {
        if (pageCount < 2) return
        val idx = pageIndex.coerceIn(0, pageCount - 1)
        val bright = rgb(WatchUiTheme.faceBright)
        val dim = rgb(WatchUiTheme.faceDim)
        val total = pageCount * PAGE_H + (pageCount - 1) * PAGE_GAP
        val y0 = if (total < 180) PAGE_BAND_MID - total / 2 else 36
        for (i in 0 until pageCount) {
            rect(
                PAGE_X,
                y0 + i * (PAGE_H + PAGE_GAP),
                PAGE_W,
                PAGE_H,
                if (i == idx) bright else dim,
                Style.FILL,
            )
        }
    }
}
