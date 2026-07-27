package slate.wire

import slate.generated.SdpWire

/** Wire COLOR operand (§4.3). */
sealed class SdpColor {
    data class Rgb565(val value: UShort) : SdpColor()
    /** Palette index 0..15 (SET_PALETTE idx). Encodes as tag index+1. */
    data class Pal(val index: Int) : SdpColor()

    fun encode(): ByteArray = when (this) {
        is Rgb565 -> byteArrayOf(
            SdpWire.ColorTag.LITERAL_RGB565.toByte(),
            (value.toInt() and 0xFF).toByte(),
            ((value.toInt() shr 8) and 0xFF).toByte(),
        )
        is Pal -> byteArrayOf((index + 1).toByte())
    }
}

fun rgb(value: Int): SdpColor.Rgb565 = SdpColor.Rgb565(value.toUShort())
fun pal(index: Int): SdpColor.Pal = SdpColor.Pal(index)

object Colors {
    val BLACK = rgb(0x0000)
    val WHITE = rgb(0xFFFF)
}

/** Wire STYLE byte (§4.3). */
@JvmInline
value class Style(val packed: UByte) {
    companion object {
        val FILL = Style(SdpWire.Style.MODE_FILL.toUByte())
        val STROKE = Style(SdpWire.Style.MODE_STROKE.toUByte())

        fun pack(mode: Int, width: Int = 1): Style {
            val w = if (width == 0) 1 else width.coerceIn(1, 15)
            return Style((mode or (w shl SdpWire.Style.WIDTH_SHIFT)).toUByte())
        }

        fun fillStroke(width: Int = 1): Style = pack(SdpWire.Style.MODE_FILL_STROKE, width)
    }
}

object Align {
    const val LEFT = SdpWire.Align.LEFT
    const val CENTER = SdpWire.Align.CENTER
    const val RIGHT = SdpWire.Align.RIGHT
}

object Font {
    /** Built-in 3×5 font (font id 0). LARGE is an alias until asset packs add sizes. */
    const val BUILTIN = 0
    const val LARGE = 0
}
