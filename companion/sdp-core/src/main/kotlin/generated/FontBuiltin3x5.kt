package slate.generated

/** Built-in font 0 — generated from shared/fonts/font0_3x5.json. */
object FontBuiltin3x5 {
    const val ID = 0
    const val CELL_WIDTH = 3
    const val CELL_HEIGHT = 5
    const val ADVANCE = 4
    const val FIRST_CODEPOINT = 45
    const val GLYPH_COUNT = 14

    data class Glyph(val codepoint: Int, val rows: ByteArray)

    val GLYPHS: List<Glyph> = listOf(
        Glyph(45, byteArrayOf(0, 0, 7, 0, 0)),
        Glyph(46, byteArrayOf(0, 0, 0, 0, 2)),
        Glyph(47, byteArrayOf(1, 1, 2, 4, 4)),
        Glyph(48, byteArrayOf(7, 5, 5, 5, 7)),
        Glyph(49, byteArrayOf(2, 2, 2, 2, 2)),
        Glyph(50, byteArrayOf(7, 1, 7, 4, 7)),
        Glyph(51, byteArrayOf(7, 1, 7, 1, 7)),
        Glyph(52, byteArrayOf(5, 5, 7, 1, 1)),
        Glyph(53, byteArrayOf(7, 4, 7, 1, 7)),
        Glyph(54, byteArrayOf(7, 4, 7, 5, 7)),
        Glyph(55, byteArrayOf(7, 1, 1, 1, 1)),
        Glyph(56, byteArrayOf(7, 5, 7, 5, 7)),
        Glyph(57, byteArrayOf(7, 5, 7, 1, 7)),
        Glyph(58, byteArrayOf(0, 2, 0, 2, 0)),
    )

    fun rowsFor(codepoint: Int): ByteArray? =
        GLYPHS.firstOrNull { it.codepoint == codepoint }?.rows
}
