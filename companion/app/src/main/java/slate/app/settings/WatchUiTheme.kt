package slate.app.settings

import slate.session.WatchSettings

/**
 * Live theme colours for phone-pushed screens (launcher, Kotlin notifications).
 * Updated whenever [WatchSettingsStore] loads or merges a payload.
 */
object WatchUiTheme {
    @Volatile
    var uiChrome: Int = WatchSettings.DEFAULT_UI_CHROME
        private set

    @Volatile
    var faceBright: Int = WatchSettings.DEFAULT_FACE_BRIGHT
        private set

    @Volatile
    var faceDim: Int = WatchSettings.DEFAULT_FACE_DIM
        private set

    fun apply(p: WatchSettings.Payload) {
        uiChrome = p.uiChrome and 0xFFFF
        faceBright = p.faceBright and 0xFFFF
        faceDim = p.faceDim and 0xFFFF
    }

    /** Expand RGB565 to opaque ARGB for Compose swatches. */
    fun rgb565ToArgb(rgb565: Int): Int {
        val v = rgb565 and 0xFFFF
        val r5 = (v shr 11) and 0x1F
        val g6 = (v shr 5) and 0x3F
        val b5 = v and 0x1F
        val r = (r5 * 255) / 31
        val g = (g6 * 255) / 63
        val b = (b5 * 255) / 31
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }

    fun argbToRgb565(argb: Int): Int {
        val r = (argb shr 16) and 0xFF
        val g = (argb shr 8) and 0xFF
        val b = argb and 0xFF
        return ((r shr 3) shl 11) or ((g shr 2) shl 5) or (b shr 3)
    }
}
