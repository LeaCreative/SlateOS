package slate.script

import slate.dsl.displayList
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb

/** Canonical Kotlin scenes — golden tests compare these to JS `slate.ui` output. */
object JsUiScenes {
    fun timerFace(remainingSec: Int, running: Boolean): ByteArray = displayList {
        palette(0, Colors.BLACK)
        palette(1, Colors.WHITE)
        palette(2, rgb(0x07E0))
        clear(pal(0))
        val label = "%02d:%02d".format(remainingSec / 60, remainingSec % 60)
        text(font = 0, x = 120, y = 80, align = Align.CENTER, color = pal(1), text = label)
        val btn = if (running) "Stop" else "Start"
        element(id = 1, x = 40, y = 160, w = 160, h = 40) {
            rectRound(40, 160, 160, 40, r = 8, color = pal(2), style = Style.FILL)
            text(font = 0, x = 120, y = 172, align = Align.CENTER, color = pal(0), text = btn)
        }
        commit()
    }
}
