package slate.app.apps

import slate.app.link.LinkLog
import slate.app.settings.WatchUiTheme
import slate.app.apps.NavChrome.pageBars
import slate.app.apps.NavChrome.sectionBars
import slate.dsl.displayList
import slate.generated.SdpWire
import slate.host.AppManifest
import slate.host.HostInbound
import slate.host.HostOutbound
import slate.host.KotlinSlateApp
import slate.host.PriorityClass
import slate.host.RefreshPolicy
import slate.wire.Align
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb

/**
 * The app drawer. Reached by swiping right-to-left on the watch.
 *
 * Windowed paging (4 outline rows per screen). A 1 px paging sentinel
 * SCROLL_REGION (empty children, tall contentH) lets older firmware update
 * scroll offset on UP/DOWN and emit changing SCROLL_POS; CompositorHost
 * translates those into swipe. Rows sit outside the region so they do not
 * visually scroll. Newer firmware with have_scroll still forwards real SWIPE.
 */
class LauncherApp(
    /** Installed JS sub-apps, newest state each time the screen is built. */
    private val listApps: () -> List<Entry>,
) : KotlinSlateApp() {

    data class Entry(val id: String, val name: String)

    override val manifest = AppManifest(
        id = APP_ID,
        name = "Apps",
        version = "1.0.0",
        minProtocolVersion = 1,
        defaultPriority = PriorityClass.NORMAL,
        refresh = RefreshPolicy.Manual,
    )

    /** Set when a row is tapped; the host reads and clears it. */
    @Volatile
    var pendingLaunchId: String? = null
        private set

    private var entries: List<Entry> = emptyList()
    private var windowStart: Int = 0

    fun takePendingLaunch(): String? {
        val id = pendingLaunchId
        pendingLaunchId = null
        return id
    }

    override fun onFocus(out: MutableList<HostOutbound>) {
        entries = listApps()
        if (windowStart >= entries.size) windowStart = 0
        LinkLog.i(
            "Launcher: onFocus — ${entries.size} JS sub-app(s): " +
                entries.joinToString { it.id },
        )
        out.push(render())
    }

    override fun onInput(msg: HostInbound.Input, out: MutableList<HostOutbound>): Boolean {
        if (msg.op == SdpWire.InputOp.SWIPE && msg.dir == SdpWire.SwipeDir.RIGHT) {
            LinkLog.i("Launcher: swipe RIGHT — closing")
            out.add(HostOutbound.RelinquishFocus)
            return true
        }
        if (msg.op == SdpWire.InputOp.SWIPE) {
            when (msg.dir) {
                // Content-scroll: finger up reveals the next page of apps.
                SdpWire.SwipeDir.UP -> {
                    LinkLog.i("Launcher: swipe UP — next page")
                    return pageDown(out)
                }
                SdpWire.SwipeDir.DOWN -> {
                    LinkLog.i("Launcher: swipe DOWN — previous page")
                    return pageUp(out)
                }
                else -> Unit
            }
        }
        if (msg.op != SdpWire.InputOp.TAP) return false
        val index = msg.elemId - ELEM_FIRST_ROW
        if (index < 0 || index >= VISIBLE) return false
        val absolute = windowStart + index
        if (absolute < 0 || absolute >= entries.size) return false
        val entry = entries[absolute]
        LinkLog.i("Launcher: tap row $absolute -> ${entry.id}")
        pendingLaunchId = entry.id
        return true
    }

    override fun onBlur(out: MutableList<HostOutbound>) {
        LinkLog.i("Launcher: onBlur")
    }

    private fun pageUp(out: MutableList<HostOutbound>): Boolean {
        if (windowStart <= 0) return true
        windowStart = (windowStart - VISIBLE).coerceAtLeast(0)
        out.push(render())
        return true
    }

    private fun pageDown(out: MutableList<HostOutbound>): Boolean {
        if (windowStart + VISIBLE >= entries.size) return true
        windowStart =
            (windowStart + VISIBLE).coerceAtMost(
                (entries.size - VISIBLE).coerceAtLeast(0),
            )
        out.push(render())
        return true
    }

    private fun render() = displayList {
        val chrome = WatchUiTheme.uiChrome
        palette(0, rgb(0x0000))
        palette(1, rgb(chrome))
        clear(pal(0))

        sectionBars(NavChrome.Section.Launcher)

        textScaled(
            font = FONT_5X7, x = 120, y = 10, align = Align.CENTER,
            color = pal(1), scale = TEXT_SCALE, text = "Apps",
        )

        val rows = entries

        if (rows.isEmpty()) {
            textScaled(
                font = FONT_5X7, x = 120, y = 110, align = Align.CENTER,
                color = pal(1), scale = TEXT_SCALE, text = "No sub-apps installed",
            )
        } else {
            val multiPage = rows.size > VISIBLE
            if (multiPage) {
                // Empty sentinel: contentH >> h so set_scroll_offset can move.
                scrollRegion(y = 0, h = 1, contentH = 240) { }
            }
            val pageCount = (rows.size + VISIBLE - 1) / VISIBLE
            val pageIndex = windowStart / VISIBLE
            pageBars(pageCount, pageIndex)

            val end = minOf(windowStart + VISIBLE, rows.size)
            for (i in windowStart until end) {
                val top = LIST_TOP + (i - windowStart) * ROW_PITCH
                val entry = rows[i]
                element(
                    id = ELEM_FIRST_ROW + (i - windowStart),
                    x = ROW_X, y = top, w = ROW_W, h = ROW_H,
                    flags = SdpWire.ElemFlags.EMIT_TOUCH or SdpWire.ElemFlags.HAPTIC,
                ) {
                    rectRound(ROW_X, top, ROW_W, ROW_H, 8, pal(1), Style.STROKE)
                    textScaled(
                        font = FONT_5X7,
                        x = 16,
                        y = top + TEXT_Y,
                        align = Align.LEFT,
                        color = pal(1),
                        scale = TEXT_SCALE,
                        text = fitLabel(entry.name, 16),
                    )
                }
            }
        }
        commit()
    }

    companion object {
        const val APP_ID = "slate.ui.launcher"

        private const val FONT_5X7 = 1
        private const val LIST_TOP = 36
        private const val VISIBLE = 4
        private const val ROW_PITCH = 48
        private const val ROW_H = 44
        private const val ROW_X = 8
        private const val ROW_W = 224
        private const val TEXT_SCALE = 2
        private const val TEXT_Y = 15

        private const val ELEM_FIRST_ROW = 100

        /** Watch fonts are ASCII; never emit U+2026. */
        fun fitLabel(raw: String, maxChars: Int): String {
            val s = raw.trim()
            if (s.length <= maxChars) return s
            if (maxChars <= 3) return s.take(maxChars)
            return s.take(maxChars - 3) + "..."
        }
    }
}

