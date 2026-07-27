package slate.dsl

import slate.generated.SdpWire
import slate.wire.Align
import slate.wire.SdpColor
import slate.wire.Style

class DisplayListBuilder {
    private val buffer = ArrayList<Byte>(128)

    fun toByteArray(): ByteArray = buffer.toByteArray()

    private fun u8(v: Int) {
        buffer.add((v and 0xFF).toByte())
    }

    private fun u16Le(v: Int) {
        u8(v)
        u8(v shr 8)
    }

    private fun op(code: Int) {
        u8(code)
    }

    private fun color(c: SdpColor) {
        buffer.addAll(c.encode().toList())
    }

    private fun style(s: Style) {
        u8(s.packed.toInt())
    }

    fun palette(idx: Int, color: SdpColor.Rgb565) {
        op(SdpWire.Op.SET_PALETTE)
        u8(idx)
        u16Le(color.value.toInt())
    }

    fun clear(color: SdpColor) {
        op(SdpWire.Op.CLEAR)
        color(color)
    }

    fun rect(x: Int, y: Int, w: Int, h: Int, color: SdpColor, style: Style = Style.FILL) {
        op(SdpWire.Op.RECT)
        u8(x); u8(y); u8(w); u8(h)
        color(color)
        style(style)
    }

    fun rectRound(
        x: Int, y: Int, w: Int, h: Int, r: Int,
        color: SdpColor, style: Style = Style.FILL,
    ) {
        op(SdpWire.Op.RECT_ROUND)
        u8(x); u8(y); u8(w); u8(h); u8(r)
        color(color)
        style(style)
    }

    fun line(x0: Int, y0: Int, x1: Int, y1: Int, color: SdpColor, width: Int = 1) {
        op(SdpWire.Op.LINE)
        u8(x0); u8(y0); u8(x1); u8(y1)
        color(color)
        u8(width)
    }

    fun circle(cx: Int, cy: Int, r: Int, color: SdpColor, style: Style = Style.FILL) {
        op(SdpWire.Op.CIRCLE)
        u8(cx); u8(cy); u8(r)
        color(color)
        style(style)
    }

    fun arc(
        cx: Int, cy: Int, r: Int, a0: Int, a1: Int,
        color: SdpColor, width: Int = 1,
    ) {
        op(SdpWire.Op.ARC)
        u8(cx); u8(cy); u8(r)
        u16Le(a0); u16Le(a1)
        color(color)
        u8(width)
    }

    fun polyline(points: List<Pair<Int, Int>>, color: SdpColor, width: Int = 1) {
        require(points.size >= 2)
        op(SdpWire.Op.POLYLINE)
        u8(points.size)
        color(color)
        u8(width)
        for ((x, y) in points) {
            u8(x); u8(y)
        }
    }

    fun clipRect(x: Int, y: Int, w: Int, h: Int) {
        op(SdpWire.Op.CLIP_RECT)
        u8(x); u8(y); u8(w); u8(h)
    }

    fun clipClear() {
        op(SdpWire.Op.CLIP_CLEAR)
    }

    fun text(
        font: Int = 0,
        x: Int,
        y: Int,
        align: Int = Align.LEFT,
        color: SdpColor,
        text: String,
    ) {
        val bytes = text.encodeToByteArray()
        op(SdpWire.Op.TEXT)
        u8(font); u8(x); u8(y)
        color(color)
        u8(align)
        u8(bytes.size)
        bytes.forEach { buffer.add(it) }
    }

    fun textBox(
        font: Int = 0,
        x: Int, y: Int, w: Int, h: Int,
        align: Int = Align.LEFT,
        flags: Int = 0,
        color: SdpColor,
        text: String,
    ) {
        val bytes = text.encodeToByteArray()
        op(SdpWire.Op.TEXT_BOX)
        u8(font); u8(x); u8(y); u8(w); u8(h)
        color(color)
        u8(align); u8(flags)
        u8(bytes.size)
        bytes.forEach { buffer.add(it) }
    }

    fun icon(atlas: Int, id: Int, x: Int, y: Int, tint: SdpColor) {
        op(SdpWire.Op.ICON)
        u8(atlas); u16Le(id); u8(x); u8(y)
        color(tint)
    }

    fun image(asset: Int, id: Int, x: Int, y: Int) {
        op(SdpWire.Op.IMAGE)
        u8(asset); u16Le(id); u8(x); u8(y)
    }

    fun progressBar(
        x: Int, y: Int, w: Int, h: Int, pct: Int,
        fg: SdpColor, bg: SdpColor,
    ) {
        op(SdpWire.Op.PROGRESS_BAR)
        u8(x); u8(y); u8(w); u8(h); u8(pct)
        color(fg); color(bg)
    }

    fun progressArc(
        cx: Int, cy: Int, r: Int, pct: Int,
        fg: SdpColor, bg: SdpColor, width: Int = 1,
    ) {
        op(SdpWire.Op.PROGRESS_ARC)
        u8(cx); u8(cy); u8(r); u8(pct)
        color(fg); color(bg)
        u8(width)
    }

    fun element(
        id: Int,
        x: Int, y: Int, w: Int, h: Int,
        flags: Int = 0,
        block: DisplayListBuilder.() -> Unit,
    ) {
        op(SdpWire.Op.BEGIN_ELEM)
        u16Le(id); u8(x); u8(y); u8(w); u8(h); u8(flags)
        block()
        op(SdpWire.Op.END_ELEM)
    }

    fun scrollRegion(y: Int, h: Int, contentH: Int, block: DisplayListBuilder.() -> Unit) {
        op(SdpWire.Op.SCROLL_REGION)
        u8(y); u8(h); u16Le(contentH)
        block()
        clipClear()
    }

    fun patch(
        slot: Int, x: Int, y: Int, w: Int, h: Int,
        format: Int, encoding: Int, data: ByteArray,
    ) {
        op(SdpWire.Op.PATCH)
        u8(slot); u8(x); u8(y); u8(w); u8(h)
        u8(format); u8(encoding)
        u16Le(data.size)
        data.forEach { buffer.add(it) }
    }

    fun patchRef(slot: Int, x: Int, y: Int) {
        op(SdpWire.Op.PATCH_REF)
        u8(slot); u8(x); u8(y)
    }

    fun haptic(pattern: Int) {
        op(SdpWire.Op.HAPTIC)
        u8(pattern)
    }

    fun backlight(level: Int) {
        op(SdpWire.Op.BACKLIGHT)
        u8(level)
    }

    fun retain(ttlSeconds: Int) {
        op(SdpWire.Op.RETAIN)
        u16Le(ttlSeconds)
    }

    fun commit(flags: Int = 0) {
        op(SdpWire.Op.COMMIT)
        u8(flags)
    }
}

fun displayList(block: DisplayListBuilder.() -> Unit): ByteArray {
    val builder = DisplayListBuilder()
    builder.block()
    return builder.toByteArray()
}
