package slate.app.apps

import slate.dsl.displayList
import slate.host.AppManifest
import slate.host.HostOutbound
import slate.host.KotlinSlateApp
import slate.host.PriorityClass
import slate.host.RefreshPolicy
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.pal
import java.time.LocalTime
import java.time.format.DateTimeFormatter

/**
 * Ambient clock reference app — refresh at most once per minute (ambient quota).
 */
class ClockApp : KotlinSlateApp() {
    override val manifest = AppManifest(
        id = "slate.ref.clock",
        name = "Clock",
        version = "1.0.0",
        minProtocolVersion = 1,
        defaultPriority = PriorityClass.AMBIENT,
        // Manual: an AMBIENT app re-renders while buried at a 1/min quota, so
        // this clock repainted over whatever sub-app was focused a moment
        // after it appeared (N-34). It stays as the stack base, but it now
        // draws only when it actually has focus.
        refresh = RefreshPolicy.Manual,
    )

    private val fmt = DateTimeFormatter.ofPattern("HH:mm")

    override fun onFocus(out: MutableList<HostOutbound>) {
        out.push(buildList())
    }

    override fun onRender(out: MutableList<HostOutbound>) {
        out.push(buildList())
    }

    private fun buildList(): ByteArray {
        val time = LocalTime.now().format(fmt)
        return displayList {
            palette(0, Colors.BLACK)
            palette(1, Colors.WHITE)
            clear(pal(0))
            text(
                font = Font.LARGE,
                x = 120,
                y = 100,
                align = Align.CENTER,
                color = pal(1),
                text = time.take(5),
            )
            commit()
        }
    }
}
