package slate.interpreter

import slate.generated.SdpWire
import slate.hit.HitRect
import slate.parse.ParseResult
import slate.parse.RenderSink
import slate.parse.SdpStatus
import slate.parse.Style
import slate.parse.parse
import slate.render.Framebuffer
import slate.render.Gfx
import slate.render.TextLayout

data class RenderOutput(
    val framebuffer: Framebuffer,
    val hitRects: List<HitRect>,
    val status: SdpStatus,
)

/**
 * Renders a display list to a 240×240 framebuffer using the same draw rules as
 * firmware src/sdp_interpreter.cpp (DrawSink).
 */
class DisplayListInterpreter {
    fun render(data: ByteArray): RenderOutput {
        val fb = Framebuffer()
        val hits = mutableListOf<HitRect>()
        val meta = MetaCollector(hits)
        val draw = DrawSink(fb)

        val validate = parse(data, execute = false, sink = null)
        if (validate.status != SdpStatus.Ok || !validate.sawCommit) {
            return RenderOutput(fb, hits, SdpStatus.Reject)
        }

        parse(data, execute = true, sink = meta)
        parse(data, execute = true, sink = draw)

        return RenderOutput(fb, hits, SdpStatus.Ok)
    }

    private class MetaCollector(private val hits: MutableList<HitRect>) : RenderSink {
        private data class Frame(val id: Int, val x: Int, val y: Int, val w: Int, val h: Int, val flags: Int)
        private val stack = ArrayDeque<Frame>()

        override fun beginElem(id: Int, x: Int, y: Int, w: Int, h: Int, flags: Int) {
            if (stack.size >= SdpWire.MAX_ELEM_DEPTH) return
            stack.addLast(Frame(id, x, y, w, h, flags))
        }

        override fun endElem() {
            if (stack.isEmpty()) return
            val f = stack.removeLast()
            if (f.flags and SdpWire.ElemFlags.NO_HIT != 0) return
            if (hits.size >= SdpWire.MAX_HIT_ELEMS) return
            hits.add(HitRect(f.id, f.x, f.y, f.w, f.h, f.flags))
        }
    }

    private class DrawSink(private val fb: Framebuffer) : RenderSink {
        var scrollY = 0
        var scrollH = 0
        var scrollOffset = 0
        var inScroll = false

        private fun mapY(y: Int): Int {
            if (!inScroll) return y
            val screen = scrollY + y - scrollOffset
            return when {
                screen < 0 -> 0
                screen > 239 -> 239
                else -> screen
            }
        }

        override fun clear(color: Int) {
            fb.clear(color)
        }

        override fun rect(x: Int, y: Int, w: Int, h: Int, color: Int, style: Style) {
            applyStyleRect(x, mapY(y), w, h, color, style)
        }

        override fun rectRound(x: Int, y: Int, w: Int, h: Int, rad: Int, color: Int, style: Style) {
            val yy = mapY(y)
            if (style.mode == SdpWire.Style.MODE_FILL || style.mode == SdpWire.Style.MODE_FILL_STROKE) {
                fb.fillRect(x, yy, w, h, color)
            }
            if (style.mode == SdpWire.Style.MODE_STROKE || style.mode == SdpWire.Style.MODE_FILL_STROKE) {
                Gfx.drawRoundRect(fb, x, yy, w, h, rad, color)
            }
        }

        override fun line(x0: Int, y0: Int, x1: Int, y1: Int, color: Int, width: Int) {
            Gfx.drawLine(fb, x0, mapY(y0), x1, mapY(y1), color)
        }

        override fun circle(cx: Int, cy: Int, r: Int, color: Int, style: Style) {
            val yy = mapY(cy)
            if (style.mode == SdpWire.Style.MODE_FILL || style.mode == SdpWire.Style.MODE_FILL_STROKE) {
                for (rr in 0..r) Gfx.drawCircle(fb, cx, yy, rr, color)
            }
            if (style.mode == SdpWire.Style.MODE_STROKE || style.mode == SdpWire.Style.MODE_FILL_STROKE) {
                Gfx.drawCircle(fb, cx, yy, r, color)
            }
        }

        override fun arc(cx: Int, cy: Int, r: Int, a0: Int, a1: Int, color: Int, width: Int) {
            Gfx.drawArc(fb, cx, mapY(cy), r, a0, a1, color)
        }

        override fun polyline(count: Int, color: Int, width: Int, xy: ByteArray) {
            for (i in 1 until count) {
                val x0 = xy[(i - 1) * 2].toInt() and 0xFF
                val y0 = mapY(xy[(i - 1) * 2 + 1].toInt() and 0xFF)
                val x1 = xy[i * 2].toInt() and 0xFF
                val y1 = mapY(xy[i * 2 + 1].toInt() and 0xFF)
                Gfx.drawLine(fb, x0, y0, x1, y1, color)
            }
        }

        override fun clipRect(x: Int, y: Int, w: Int, h: Int) {
            fb.setClip(x, mapY(y), w, h)
        }

        override fun clipClear() {
            fb.clearClip()
            inScroll = false
        }

        override fun text(font: Int, x: Int, y: Int, color: Int, align: Int, len: Int, utf8: ByteArray) {
            TextLayout.drawTextRun(fb, x, mapY(y), color, align, utf8.decodeToString())
        }

        override fun textScaled(
            font: Int, x: Int, y: Int, color: Int, align: Int, scale: Int,
            len: Int, utf8: ByteArray,
        ) {
            TextLayout.drawTextRunScaled(
                fb, x, mapY(y), color, align, utf8.decodeToString(), font, scale,
            )
        }

        override fun textBox(
            font: Int, x: Int, y: Int, w: Int, h: Int,
            color: Int, align: Int, flags: Int, len: Int, utf8: ByteArray,
        ) {
            TextLayout.drawTextRun(fb, x, mapY(y), color, align, utf8.decodeToString())
        }

        override fun icon(atlas: Int, id: Int, x: Int, y: Int, tint: Int) {
            fb.fillRect(x, mapY(y), 8, 8, tint)
        }

        override fun image(asset: Int, id: Int, x: Int, y: Int) {
            fb.fillRect(x, mapY(y), 16, 16, 0x7BEF)
        }

        override fun progressBar(x: Int, y: Int, w: Int, h: Int, pct: Int, fg: Int, bg: Int) {
            val yy = mapY(y)
            fb.fillRect(x, yy, w, h, bg)
            val fw = (w * pct) / 100
            if (fw > 0) fb.fillRect(x, yy, fw, h, fg)
        }

        override fun progressArc(cx: Int, cy: Int, r: Int, pct: Int, fg: Int, bg: Int, width: Int) {
            val yy = mapY(cy)
            Gfx.drawArc(fb, cx, yy, r, 0, 359, bg)
            val a1 = (360 * pct) / 100
            if (a1 > 0) Gfx.drawArc(fb, cx, yy, r, 0, a1, fg)
        }

        override fun patch(
            slot: Int, x: Int, y: Int, w: Int, h: Int,
            format: Int, encoding: Int, len: Int, data: ByteArray,
        ) {
            if (encoding != SdpWire.PatchEncoding.RAW) return
            val yy = mapY(y)
            when (format) {
                SdpWire.PatchFormat.RGB565 -> {
                    val need = w * h * 2
                    if (len < need) return
                    for (row in 0 until h) {
                        for (col in 0 until w) {
                            val i = (row * w + col) * 2
                            val pix = (data[i].toInt() and 0xFF) or
                                ((data[i + 1].toInt() and 0xFF) shl 8)
                            fb.putPixel(x + col, yy + row, pix)
                        }
                    }
                }
                SdpWire.PatchFormat.RGB332 -> {
                    if (len < w * h) return
                    Gfx.blitRgb332(fb, x, yy, w, h, data)
                }
                SdpWire.PatchFormat.MONO1 -> {
                    val need = (w * h + 7) / 8
                    if (len < need) return
                    Gfx.blit1bit(fb, x, yy, w, h, data, 0xFFFF, 0x0000)
                }
            }
        }

        override fun scrollRegion(y: Int, h: Int, contentH: Int) {
            scrollY = y
            scrollH = h
            inScroll = true
            fb.setClip(0, y, SdpWire.DISPLAY_SIZE, h)
        }

        private fun strokeRect(x: Int, y: Int, w: Int, h: Int, color: Int, width: Int) {
            for (i in 0 until width) {
                if (w <= 2 * i || h <= 2 * i) break
                Gfx.drawRoundRect(fb, x + i, y + i, w - 2 * i, h - 2 * i, 0, color)
            }
        }

        private fun applyStyleRect(x: Int, y: Int, w: Int, h: Int, color: Int, style: Style) {
            if (style.mode == SdpWire.Style.MODE_FILL || style.mode == SdpWire.Style.MODE_FILL_STROKE) {
                fb.fillRect(x, y, w, h, color)
            }
            if (style.mode == SdpWire.Style.MODE_STROKE || style.mode == SdpWire.Style.MODE_FILL_STROKE) {
                strokeRect(x, y, w, h, color, style.width)
            }
        }
    }
}
