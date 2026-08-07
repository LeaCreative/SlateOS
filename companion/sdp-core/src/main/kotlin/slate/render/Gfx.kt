package slate.render

import slate.generated.FontBuiltin3x5
import slate.generated.FontBuiltin5x7
import slate.generated.SdpWire

/** RGB565 helpers matching firmware renderer.hpp. */
object Rgb565 {
    fun from888(r: Int, g: Int, b: Int): Int =
        ((r and 0x1F) shl 11) or ((g and 0x3F) shl 5) or (b and 0x1F)

    fun toArgb(rgb565: Int): Int {
        val r5 = (rgb565 shr 11) and 0x1F
        val g6 = (rgb565 shr 5) and 0x3F
        val b5 = rgb565 and 0x1F
        val r = (r5 shl 3) or (r5 shr 2)
        val g = (g6 shl 2) or (g6 shr 4)
        val b = (b5 shl 3) or (b5 shr 2)
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }

    fun rgb332To565(c: Int): Int {
        val r3 = (c shr 5) and 0x07
        val g3 = (c shr 2) and 0x07
        val b2 = c and 0x03
        val r = (r3 shl 2) or (r3 shr 1)
        val g = (g3 shl 3) or g3
        val b = (b2 shl 3) or (b2 shl 1) or (b2 shr 1)
        return from888(r, g, b)
    }
}

class Framebuffer(val width: Int = SdpWire.DISPLAY_SIZE, val height: Int = SdpWire.DISPLAY_SIZE) {
  val pixels = IntArray(width * height)

    var clipX = 0
    var clipY = 0
    var clipW = 0
    var clipH = 0

    fun clear(color565: Int) {
        val argb = Rgb565.toArgb(color565)
        for (i in pixels.indices) pixels[i] = argb
    }

    fun setClip(x: Int, y: Int, w: Int, h: Int) {
        clipX = x; clipY = y; clipW = w; clipH = h
    }

    fun clearClip() {
        clipW = 0; clipH = 0
    }

    private fun inClip(x: Int, y: Int): Boolean {
        if (clipW == 0) return true
        return x >= clipX && x < clipX + clipW && y >= clipY && y < clipY + clipH
    }

    fun putPixel(x: Int, y: Int, color565: Int) {
        if (x !in 0 until width || y !in 0 until height) return
        if (!inClip(x, y)) return
        pixels[y * width + x] = Rgb565.toArgb(color565)
    }

    fun fillRect(x: Int, y: Int, w: Int, h: Int, color565: Int) {
        val x1 = minOf(x + w, width) - 1
        val y1 = minOf(y + h, height) - 1
        if (x >= width || y >= height) return
        for (py in y..y1) {
            for (px in x..x1) putPixel(px, py, color565)
        }
    }
}

/** Low-level drawing — algorithms ported from src/renderer.cpp. */
object Gfx {
    private val sinTable = intArrayOf(
        0, 4, 9, 13, 18, 22, 27, 31, 36, 40, 44, 49, 53, 57, 62,
        66, 70, 74, 79, 83, 87, 91, 95, 99, 103, 107, 111, 115, 119, 122,
        126, 130, 133, 137, 141, 144, 147, 151, 154, 158, 161, 164, 167, 171, 174,
        177, 180, 182, 185, 188, 191, 193, 196, 198, 201, 203, 205, 208, 210, 212,
        214, 216, 218, 219, 221, 223, 224, 226, 227, 228, 230, 231, 232, 233, 234,
        235, 236, 237, 237, 238, 239, 239, 240, 240, 241, 241, 241, 242, 242, 242,
        242,
    )

    fun isin(deg: Int): Int {
        var d = deg % 360
        if (d < 0) d += 360
        return when {
            d <= 90 -> sinTable[d]
            d <= 180 -> sinTable[180 - d]
            d <= 270 -> -sinTable[d - 180]
            else -> -sinTable[360 - d]
        }
    }

    fun icos(deg: Int): Int = isin(deg + 90)

    fun drawLine(fb: Framebuffer, x0: Int, y0: Int, x1: Int, y1: Int, color: Int) {
        var cx = x0; var cy = y0
        val dx = kotlin.math.abs(x1 - x0)
        val dy = -kotlin.math.abs(y1 - y0)
        val sx = if (x0 < x1) 1 else -1
        val sy = if (y0 < y1) 1 else -1
        var err = dx + dy
        while (true) {
            if (cx in 0 until fb.width && cy in 0 until fb.height) {
                fb.putPixel(cx, cy, color)
            }
            if (cx == x1 && cy == y1) break
            val e2 = 2 * err
            if (e2 >= dy) { err += dy; cx += sx }
            if (e2 <= dx) { err += dx; cy += sy }
        }
    }

    fun drawCircle(fb: Framebuffer, cx: Int, cy: Int, radius: Int, color: Int) {
        var x = radius
        var y = 0
        var err = 0
        while (x >= y) {
            plot8(fb, cx, cy, x, y, color)
            y++
            if (err <= 0) err += 2 * y + 1 else { x--; err += 2 * (y - x) + 1 }
        }
    }

    private fun plot8(fb: Framebuffer, cx: Int, cy: Int, dx: Int, dy: Int, color: Int) {
        fun px(px: Int, py: Int) {
            if (px in 0 until fb.width && py in 0 until fb.height) fb.putPixel(px, py, color)
        }
        px(cx + dx, cy + dy); px(cx - dx, cy + dy)
        px(cx + dx, cy - dy); px(cx - dx, cy - dy)
        px(cx + dy, cy + dx); px(cx - dy, cy + dx)
        px(cx + dy, cy - dx); px(cx - dy, cy - dx)
    }

    fun drawRoundRect(fb: Framebuffer, x: Int, y: Int, w: Int, h: Int, radius: Int, color: Int) {
        if (radius == 0) {
            fb.fillRect(x, y, w, 1, color)
            fb.fillRect(x, y + h - 1, w, 1, color)
            fb.fillRect(x, y, 1, h, color)
            fb.fillRect(x + w - 1, y, 1, h, color)
            return
        }
        val r = minOf(radius, minOf(w, h) / 2)
        fb.fillRect(x + r, y, w - 2 * r, 1, color)
        fb.fillRect(x + r, y + h - 1, w - 2 * r, 1, color)
        fb.fillRect(x, y + r, 1, h - 2 * r, color)
        fb.fillRect(x + w - 1, y + r, 1, h - 2 * r, color)
        corner(fb, x + r, y + r, r, false, false, color)
        corner(fb, x + w - 1 - r, y + r, r, true, false, color)
        corner(fb, x + r, y + h - 1 - r, r, false, true, color)
        corner(fb, x + w - 1 - r, y + h - 1 - r, r, true, true, color)
    }

    private fun corner(
        fb: Framebuffer, ccx: Int, ccy: Int, r: Int,
        qx: Boolean, qy: Boolean, color: Int,
    ) {
        var cx2 = r
        var cy2 = 0
        var err2 = 0
        while (cx2 >= cy2) {
            fun plot(dx: Int, dy: Int) {
                val px = ccx + if (qx) dx else -dx
                val py = ccy + if (qy) dy else -dy
                if (px in 0 until fb.width && py in 0 until fb.height) {
                    fb.putPixel(px, py, color)
                }
            }
            plot(cx2, cy2); plot(cy2, cx2)
            cy2++
            if (err2 <= 0) err2 += 2 * cy2 + 1 else { cx2--; err2 += 2 * (cy2 - cx2) + 1 }
        }
    }

    fun drawArc(fb: Framebuffer, cx: Int, cy: Int, radius: Int, a0: Int, a1: Int, color: Int) {
        var deg = a0
        while (deg != a1) {
            val px = cx + (icos(deg) * radius) / 256
            val py = cy - (isin(deg) * radius) / 256
            if (px in 0 until fb.width && py in 0 until fb.height) {
                fb.putPixel(px, py, color)
            }
            deg = (deg + 1) % 360
        }
    }

    fun blitRgb332(fb: Framebuffer, x: Int, y: Int, w: Int, h: Int, src: ByteArray) {
        for (dy in 0 until h) {
            for (dx in 0 until w) {
                val c = src[dy * w + dx].toInt() and 0xFF
                fb.putPixel(x + dx, y + dy, Rgb565.rgb332To565(c))
            }
        }
    }

    fun blit1bit(fb: Framebuffer, x: Int, y: Int, w: Int, h: Int, src: ByteArray, fg: Int, bg: Int) {
        for (dy in 0 until h) {
            for (dx in 0 until w) {
                val bitIdx = dy * w + dx
                val set = (src[bitIdx / 8].toInt() shr (7 - (bitIdx % 8))) and 1 != 0
                fb.putPixel(x + dx, y + dy, if (set) fg else bg)
            }
        }
    }
}

object TextLayout {
    /**
     * A built-in font, selected by the id every SDP text op carries.
     *
     * 0 is the 3x5 (dense — the diagnostic overlay); 1 the 5x7 (legible —
     * anything a person reads). This used to hardcode font 0, so a font-1
     * screen previewed on the phone looked nothing like the watch.
     */
    data class Font(
        val cellWidth: Int,
        val cellHeight: Int,
        val advance: Int,
        val rowsFor: (Int) -> ByteArray?,
    )

    private val FONT_0 = Font(
        FontBuiltin3x5.CELL_WIDTH, FontBuiltin3x5.CELL_HEIGHT,
        FontBuiltin3x5.ADVANCE, FontBuiltin3x5::rowsFor,
    )
    private val FONT_1 = Font(
        FontBuiltin5x7.CELL_WIDTH, FontBuiltin5x7.CELL_HEIGHT,
        FontBuiltin5x7.ADVANCE, FontBuiltin5x7::rowsFor,
    )

    /** Unknown ids fall back to 0, matching font::describe() on the watch. */
    fun font(id: Int): Font = if (id == 1) FONT_1 else FONT_0

    /**
     * Ink width of a run, matching the firmware exactly.
     *
     * The trailing advance is inter-glyph spacing, not ink, so the last cell
     * is measured rather than its gap — `draw_text_run` in sdp_interpreter.cpp
     * does the same. Getting this wrong shifts every centred string.
     */
    fun pixelWidth(fontId: Int, scale: Int, text: String): Int {
        val f = font(fontId)
        if (text.isEmpty()) return 0
        return (text.length - 1) * f.advance * scale + f.cellWidth * scale
    }

    fun drawChar(fb: Framebuffer, x: Int, y: Int, c: Char, color: Int, fontId: Int = 0) {
        val f = font(fontId)
        val rows = f.rowsFor(c.code)
        if (rows == null) {
            fb.fillRect(x, y, f.cellWidth, f.cellHeight, color)
            return
        }
        for (row in 0 until f.cellHeight) {
            val g = rows[row].toInt() and 0xFF
            for (col in 0 until f.cellWidth) {
                if (g and (1 shl (f.cellWidth - 1 - col)) != 0) {
                    fb.putPixel(x + col, y + row, color)
                }
            }
        }
    }

    fun drawTextRun(
        fb: Framebuffer, x: Int, y: Int, color: Int, align: Int, text: String,
        fontId: Int = 0,
    ) = drawTextRunScaled(fb, x, y, color, align, text, fontId, scale = 1)

    /**
     * One glyph, each source pixel drawn as a `scale` x `scale` block.
     *
     * Mirrors `draw_glyph_scaled` in sdp_interpreter.cpp. Scale 1 is the same
     * pixels as [drawChar], so the unscaled path stays byte-identical.
     */
    fun drawCharScaled(
        fb: Framebuffer, x: Int, y: Int, c: Char, color: Int, fontId: Int, scale: Int,
    ) {
        val s = if (scale < 1) 1 else scale
        if (s == 1) {
            drawChar(fb, x, y, c, color, fontId)
            return
        }
        val f = font(fontId)
        val rows = f.rowsFor(c.code)
        if (rows == null) {
            fb.fillRect(x, y, f.cellWidth * s, f.cellHeight * s, color)
            return
        }
        for (row in 0 until f.cellHeight) {
            val g = rows[row].toInt() and 0xFF
            for (col in 0 until f.cellWidth) {
                if (g and (1 shl (f.cellWidth - 1 - col)) != 0) {
                    fb.fillRect(x + col * s, y + row * s, s, s, color)
                }
            }
        }
    }

    /**
     * TEXT_SCALED (0xE0) on the desktop.
     *
     * The Kotlin parser skipped this opcode entirely until 7 Aug 2026 — legal,
     * since 0xE0-0xEF is the length-prefixed extension range old firmware is
     * *meant* to skip, but it made every preview PNG and golden image of a
     * modern sub-app quietly wrong. Every sub-app now draws its text this way,
     * because the 3x5 base font is not legible at arm's length, so "skipped"
     * meant "the desktop renderer showed no text at all".
     */
    fun drawTextRunScaled(
        fb: Framebuffer, x: Int, y: Int, color: Int, align: Int, text: String,
        fontId: Int, scale: Int,
    ) {
        val s = if (scale < 1) 1 else scale
        val f = font(fontId)
        val pixelW = pixelWidth(fontId, s, text)
        var startX = x
        when (align) {
            SdpWire.Align.CENTER -> startX = if (pixelW / 2 < x) x - pixelW / 2 else 0
            SdpWire.Align.RIGHT -> startX = if (pixelW < x) x - pixelW else 0
        }
        for (i in text.indices) {
            drawCharScaled(fb, startX + i * f.advance * s, y, text[i], color, fontId, s)
        }
    }
}
