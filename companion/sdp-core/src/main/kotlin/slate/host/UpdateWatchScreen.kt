package slate.host

import slate.dsl.displayList
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.pal

/** Built-in screen shown when an app's minProtocolVersion exceeds the watch. */
object UpdateWatchScreen {
    fun displayListBytes(appName: String, required: Int, have: Int): ByteArray =
        displayList {
            palette(0, Colors.BLACK)
            palette(1, Colors.WHITE)
            clear(pal(0))
            text(
                font = Font.LARGE,
                x = 120,
                y = 70,
                align = Align.CENTER,
                color = pal(1),
                text = "Update",
            )
            text(
                font = Font.LARGE,
                x = 120,
                y = 100,
                align = Align.CENTER,
                color = pal(1),
                text = "watch",
            )
            text(
                font = Font.LARGE,
                x = 120,
                y = 150,
                align = Align.CENTER,
                color = pal(1),
                text = appName.take(12),
            )
            text(
                font = Font.LARGE,
                x = 120,
                y = 190,
                align = Align.CENTER,
                color = pal(1),
                text = "need v$required/$have",
            )
            commit()
        }
}
