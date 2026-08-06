package slate.app.apps

import slate.app.link.LinkLog
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
 * Lists the installed **JS** sub-apps, one full-width button each, and reports
 * which one was tapped so the host can focus it. Kotlin apps (this one, the
 * clock, notifications, the test probe) are deliberately absent — the list
 * comes from the installed-package store, which only ever holds downloaded JS.
 *
 * Geometry is dictated by the watch's scroll step. `InputRouter` moves a
 * retained list by a fixed 24 px per flick, so rows are 72 px — exactly three
 * steps — and a flick can never leave a row straddling the top edge. Three
 * rows fill the 216 px below the header; anything beyond that scrolls, and the
 * scrolling is local to the watch with no round trip.
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

    fun takePendingLaunch(): String? {
        val id = pendingLaunchId
        pendingLaunchId = null
        return id
    }

    override fun onFocus(out: MutableList<HostOutbound>) {
        entries = listApps()
        LinkLog.i("Launcher: onFocus — ${entries.size} JS sub-app(s): " +
            entries.joinToString { it.id })
        out.push(render())
    }

    override fun onInput(msg: HostInbound.Input, out: MutableList<HostOutbound>): Boolean {
        // Swipe back the way you came. The drawer is opened by a right-to-left
        // swipe, so left-to-right closes it — without needing the side button
        // or the left-edge BACK gesture, neither of which is discoverable.
        if (msg.op == SdpWire.InputOp.SWIPE && msg.dir == SdpWire.SwipeDir.RIGHT) {
            LinkLog.i("Launcher: swipe RIGHT — closing")
            out.add(HostOutbound.RelinquishFocus)
            return true
        }
        if (msg.op != SdpWire.InputOp.TAP) return false
        val index = msg.elemId - ELEM_FIRST_ROW
        if (index < 0 || index >= entries.size) return false
        val entry = entries[index]
        LinkLog.i("Launcher: tap row $index -> ${entry.id}")
        // The host does the focus switch: a sub-app cannot focus another app,
        // and this one is a sub-app like any other.
        pendingLaunchId = entry.id
        return true
    }

    override fun onBlur(out: MutableList<HostOutbound>) {
        LinkLog.i("Launcher: onBlur")
    }

    private fun render() = displayList {
        palette(0, rgb(0x0000))          // background
        palette(1, rgb(0xFFFF))          // label
        palette(2, rgb(0x1082))          // button fill
        palette(3, rgb(0x4A69))          // button edge
        clear(pal(0))

        // Header sits above the scroll region so it stays put while the list moves.
        textScaled(
            font = FONT_5X7, x = 120, y = 6, align = Align.CENTER,
            color = pal(1), scale = 2, text = "Apps",
        )

        val rows = entries
        val contentH = maxOf(rows.size * ROW_PITCH, LIST_H)
        scrollRegion(y = LIST_TOP, h = LIST_H, contentH = contentH) {
            if (rows.isEmpty()) {
                textScaled(
                    font = FONT_5X7, x = 120, y = 40, align = Align.CENTER,
                    color = pal(1), scale = 2, text = "No sub-apps installed",
                )
            }
            rows.forEachIndexed { i, entry ->
                val top = i * ROW_PITCH
                // The whole row is the tap target, not a small square: taps are
                // not pixel-accurate (6 hits from 8 touches on the first
                // hardware run), so the button is as large as the row allows.
                element(
                    id = ELEM_FIRST_ROW + i,
                    x = ROW_X, y = top, w = ROW_W, h = ROW_H,
                    flags = SdpWire.ElemFlags.EMIT_TOUCH or SdpWire.ElemFlags.HAPTIC,
                ) {
                    rectRound(ROW_X, top, ROW_W, ROW_H, 8, pal(3), Style.FILL)
                    rectRound(ROW_X + 1, top + 1, ROW_W - 2, ROW_H - 2, 7, pal(2), Style.FILL)
                    // Centred by the watch, not by us — draw_text_run does the
                    // alignment, so there is no width calculation to get wrong
                    // and no way for phone and watch to disagree about it.
                    textScaled(
                        font = FONT_5X7,
                        x = 120,
                        y = top + (ROW_H - TEXT_H) / 2,
                        align = Align.CENTER,
                        color = pal(1),
                        scale = TEXT_SCALE,
                        text = entry.name,
                    )
                }
            }
        }
        commit()
    }

    companion object {
        const val APP_ID = "slate.ui.launcher"

        /** Built-in 5x7. Font 0 has no legible lowercase at this size. */
        private const val FONT_5X7 = 1

        // Coordinates INSIDE scrollRegion are content-relative, starting at
        // 0: the interpreter's map_y computes scroll_y + y - scroll_offset, so
        // adding LIST_TOP here counted the region origin twice and pushed the
        // last row off the bottom.
        //
        // 24 px is InputRouter's scroll step; every vertical figure here is a
        // multiple of it so scrolling always lands on a row boundary.
        private const val LIST_TOP = 24
        private const val LIST_H = 216
        private const val ROW_PITCH = 72
        private const val ROW_H = 64
        private const val ROW_X = 8
        private const val ROW_W = 224
        private const val TEXT_SCALE = 2
        private const val TEXT_H = 7 * TEXT_SCALE

        private const val ELEM_FIRST_ROW = 100
    }
}
