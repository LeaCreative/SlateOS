package slate.parse

import slate.generated.SdpWire

enum class SdpStatus { Ok, Truncated, Reject }

data class Style(val mode: Int = SdpWire.Style.MODE_FILL, val width: Int = 1)

class Palette {
    val entry = IntArray(SdpWire.PALETTE_SIZE)
    val set = BooleanArray(SdpWire.PALETTE_SIZE)
}

class Reader(private val data: ByteArray) {
    var pos = 0
    var status: SdpStatus = SdpStatus.Ok

    fun remaining(): Int = if (pos <= data.size) data.size - pos else 0

    fun ok(): Boolean = status == SdpStatus.Ok

    fun reject() {
        if (status == SdpStatus.Ok) status = SdpStatus.Reject
    }

    fun truncated() {
        if (status == SdpStatus.Ok) status = SdpStatus.Truncated
    }

    /**
     * Rewind to a position already read, so a length-prefixed payload can be
     * decoded and then skipped by its declared length rather than by however
     * far the decode happened to get.
     */
    fun seek(p: Int) {
        if (p in 0..data.size) pos = p
    }

    private fun need(n: Int): Boolean {
        if (!ok()) return false
        if (remaining() < n) {
            truncated()
            return false
        }
        return true
    }

    fun takeU8(): Int {
        if (!need(1)) return 0
        return data[pos++].toInt() and 0xFF
    }

    fun takeU16Le(): Int {
        if (!need(2)) return 0
        val lo = data[pos++].toInt() and 0xFF
        val hi = data[pos++].toInt() and 0xFF
        return lo or (hi shl 8)
    }

    fun takeBytes(n: Int): ByteArray? {
        if (!need(n)) return null
        val slice = data.copyOfRange(pos, pos + n)
        pos += n
        return slice
    }

    fun skip(n: Int) {
        if (!need(n)) return
        pos += n
    }
}

fun checkCoord(v: Int): Boolean = v < SdpWire.DISPLAY_SIZE

fun checkRect(x: Int, y: Int, w: Int, h: Int): Boolean {
    if (!checkCoord(x) || !checkCoord(y)) return false
    if (w == 0 || h == 0) return false
    if (x + w > SdpWire.DISPLAY_SIZE || y + h > SdpWire.DISPLAY_SIZE) return false
    return true
}

fun decodeColor(r: Reader, pal: Palette): Int? {
    val tag = r.takeU8()
    if (!r.ok()) return null
    if (tag == SdpWire.ColorTag.LITERAL_RGB565) {
        val rgb = r.takeU16Le()
        if (!r.ok()) return null
        return rgb
    }
    if (tag in SdpWire.ColorTag.PALETTE_MIN..SdpWire.ColorTag.PALETTE_MAX) {
        val idx = tag - 1
        if (!pal.set[idx]) {
            r.reject()
            return null
        }
        return pal.entry[idx]
    }
    r.reject()
    return null
}

fun decodeStyle(r: Reader): Style? {
    val raw = r.takeU8()
    if (!r.ok()) return null
    if (raw and SdpWire.Style.RESERVED_MASK != 0) {
        r.reject()
        return null
    }
    val mode = raw and SdpWire.Style.MODE_MASK
    if (mode == SdpWire.Style.MODE_RESERVED) {
        r.reject()
        return null
    }
    var width = (raw shr SdpWire.Style.WIDTH_SHIFT) and SdpWire.Style.WIDTH_MASK
    if (width == 0) width = 1
    return Style(mode, width)
}

data class ParseResult(
    var status: SdpStatus = SdpStatus.Ok,
    var opsConsumed: Int = 0,
    var bytesConsumed: Int = 0,
    var sawCommit: Boolean = false,
    var commitFlags: Int = 0,
)

interface RenderSink {
    fun clear(color: Int) {}
    fun setPalette(idx: Int, rgb: Int) {}
    fun rect(x: Int, y: Int, w: Int, h: Int, color: Int, style: Style) {}
    fun rectRound(x: Int, y: Int, w: Int, h: Int, rad: Int, color: Int, style: Style) {}
    fun line(x0: Int, y0: Int, x1: Int, y1: Int, color: Int, width: Int) {}
    fun circle(cx: Int, cy: Int, r: Int, color: Int, style: Style) {}
    fun arc(cx: Int, cy: Int, r: Int, a0: Int, a1: Int, color: Int, width: Int) {}
    fun polyline(count: Int, color: Int, width: Int, xy: ByteArray) {}
    fun clipRect(x: Int, y: Int, w: Int, h: Int) {}
    fun clipClear() {}
    fun text(font: Int, x: Int, y: Int, color: Int, align: Int, len: Int, utf8: ByteArray) {}

    /**
     * TEXT_SCALED (0xE0). Default no-op so a sink that predates the extension
     * keeps compiling and keeps behaving exactly as it did.
     */
    fun textScaled(
        font: Int, x: Int, y: Int, color: Int, align: Int, scale: Int,
        len: Int, utf8: ByteArray,
    ) {}
    fun textBox(
        font: Int, x: Int, y: Int, w: Int, h: Int,
        color: Int, align: Int, flags: Int, len: Int, utf8: ByteArray,
    ) {}
    fun icon(atlas: Int, id: Int, x: Int, y: Int, tint: Int) {}
    fun image(asset: Int, id: Int, x: Int, y: Int) {}
    fun progressBar(x: Int, y: Int, w: Int, h: Int, pct: Int, fg: Int, bg: Int) {}
    fun progressArc(cx: Int, cy: Int, r: Int, pct: Int, fg: Int, bg: Int, width: Int) {}
    fun beginElem(id: Int, x: Int, y: Int, w: Int, h: Int, flags: Int) {}
    fun endElem() {}
    fun scrollRegion(y: Int, h: Int, contentH: Int) {}
    fun patch(
        slot: Int, x: Int, y: Int, w: Int, h: Int,
        format: Int, encoding: Int, len: Int, data: ByteArray,
    ) {}
    fun patchRef(slot: Int, x: Int, y: Int) {}
    fun haptic(pattern: Int) {}
    fun backlight(level: Int) {}
    fun commit(flags: Int) {}
    fun retain(ttl: Int) {}
}

/**
 * TEXT_SCALED payload: font, x, y, colour, align, scale, len, bytes — matching
 * `sdp_parser.cpp` and the JS builder in shared-js/slate_ui.js.
 *
 * Best-effort by design. The caller has already captured the payload length and
 * will skip to it whatever happens here, so a malformed extension degrades to a
 * missing glyph run rather than a desynchronised stream. It deliberately does
 * not call `reject()`: a display list the watch renders happily must not be
 * reported as rejected by the desktop interpreter.
 */
private fun decodeTextScaled(r: Reader, pal: Palette, len: Int, sink: RenderSink) {
    if (len < 6) return
    val font = r.takeU8()
    val x = r.takeU8()
    val y = r.takeU8()
    val color = decodeColor(r, pal) ?: return
    val align = r.takeU8()
    val scale = r.takeU8()
    val textLen = r.takeU8()
    if (!r.ok() || textLen <= 0) return
    val bytes = ByteArray(textLen)
    for (i in 0 until textLen) {
        bytes[i] = r.takeU8().toByte()
        if (!r.ok()) return
    }
    sink.textScaled(font, x, y, color, align, scale, textLen, bytes)
}

fun parse(
    data: ByteArray,
    execute: Boolean,
    sink: RenderSink?,
    maxOps: Int = SdpWire.MAX_OPS,
    maxBytes: Int = SdpWire.MAX_LIST_BYTES,
): ParseResult {
    val out = ParseResult()
    if (data.size > maxBytes) {
        out.status = SdpStatus.Reject
        return out
    }

    val r = Reader(data)
    val pal = Palette()
    var elemDepth = 0
    var committed = false

    while (r.ok() && r.remaining() > 0) {
        if (committed) {
            r.reject()
            break
        }
        if (out.opsConsumed >= maxOps) {
            r.reject()
            break
        }

        val opcode = r.takeU8()
        if (!r.ok()) break
        out.opsConsumed++

        if (opcode in SdpWire.Op.EXT_MIN..SdpWire.Op.EXT_MAX) {
            val len = r.takeU16Le()
            if (!r.ok()) break
            val payloadStart = r.pos
            // Extensions are length-prefixed precisely so an implementation
            // that does not know one can step over it — that is the contract
            // for 0xE0..0xEF and why old firmware survives a new primitive.
            // Ones we DO know are decoded from the payload and then skipped to
            // the declared length regardless, so a decode that disagrees with
            // the writer costs one wrong op rather than desynchronising the
            // whole stream.
            if (opcode == SdpWire.Op.TEXT_SCALED && execute && sink != null) {
                decodeTextScaled(r, pal, len, sink)
            }
            r.seek(payloadStart)
            r.skip(len)
            continue
        }

        when (opcode) {
            SdpWire.Op.CLEAR -> {
                val color = decodeColor(r, pal) ?: break
                if (execute && sink != null) sink.clear(color)
            }
            SdpWire.Op.SET_PALETTE -> {
                val idx = r.takeU8()
                val rgb = r.takeU16Le()
                if (!r.ok()) break
                if (idx >= SdpWire.PALETTE_SIZE) { r.reject(); break }
                pal.entry[idx] = rgb
                pal.set[idx] = true
                if (execute && sink != null) sink.setPalette(idx, rgb)
            }
            SdpWire.Op.RECT -> {
                val x = r.takeU8(); val y = r.takeU8(); val w = r.takeU8(); val h = r.takeU8()
                if (!r.ok()) break
                if (!checkRect(x, y, w, h)) { r.reject(); break }
                val color = decodeColor(r, pal) ?: break
                val st = decodeStyle(r) ?: break
                if (execute && sink != null) sink.rect(x, y, w, h, color, st)
            }
            SdpWire.Op.RECT_ROUND -> {
                val x = r.takeU8(); val y = r.takeU8(); val w = r.takeU8(); val h = r.takeU8()
                val rad = r.takeU8()
                if (!r.ok()) break
                if (!checkRect(x, y, w, h)) { r.reject(); break }
                val color = decodeColor(r, pal) ?: break
                val st = decodeStyle(r) ?: break
                if (execute && sink != null) sink.rectRound(x, y, w, h, rad, color, st)
            }
            SdpWire.Op.LINE -> {
                val x0 = r.takeU8(); val y0 = r.takeU8(); val x1 = r.takeU8(); val y1 = r.takeU8()
                if (!r.ok()) break
                if (!checkCoord(x0) || !checkCoord(y0) || !checkCoord(x1) || !checkCoord(y1)) {
                    r.reject(); break
                }
                val color = decodeColor(r, pal) ?: break
                val width = r.takeU8()
                if (!r.ok()) break
                if (execute && sink != null) sink.line(x0, y0, x1, y1, color, width)
            }
            SdpWire.Op.CIRCLE -> {
                val cx = r.takeU8(); val cy = r.takeU8(); val rad = r.takeU8()
                if (!r.ok()) break
                if (!checkCoord(cx) || !checkCoord(cy)) { r.reject(); break }
                val color = decodeColor(r, pal) ?: break
                val st = decodeStyle(r) ?: break
                if (execute && sink != null) sink.circle(cx, cy, rad, color, st)
            }
            SdpWire.Op.ARC -> {
                val cx = r.takeU8(); val cy = r.takeU8(); val rad = r.takeU8()
                val a0 = r.takeU16Le(); val a1 = r.takeU16Le()
                if (!r.ok()) break
                if (!checkCoord(cx) || !checkCoord(cy) || a0 >= 360 || a1 >= 360) {
                    r.reject(); break
                }
                val color = decodeColor(r, pal) ?: break
                val width = r.takeU8()
                if (!r.ok()) break
                if (execute && sink != null) sink.arc(cx, cy, rad, a0, a1, color, width)
            }
            SdpWire.Op.POLYLINE -> {
                val count = r.takeU8()
                if (!r.ok()) break
                if (count < 2) { r.reject(); break }
                val color = decodeColor(r, pal) ?: break
                val width = r.takeU8()
                if (!r.ok()) break
                val pts = r.takeBytes(count * 2) ?: break
                for (i in 0 until count) {
                    val px = pts[i * 2].toInt() and 0xFF
                    val py = pts[i * 2 + 1].toInt() and 0xFF
                    if (!checkCoord(px) || !checkCoord(py)) { r.reject(); break }
                }
                if (!r.ok() || out.status == SdpStatus.Reject) break
                if (execute && sink != null) sink.polyline(count, color, width, pts)
            }
            SdpWire.Op.CLIP_RECT -> {
                val x = r.takeU8(); val y = r.takeU8(); val w = r.takeU8(); val h = r.takeU8()
                if (!r.ok()) break
                if (!checkRect(x, y, w, h)) { r.reject(); break }
                if (execute && sink != null) sink.clipRect(x, y, w, h)
            }
            SdpWire.Op.CLIP_CLEAR -> {
                if (execute && sink != null) sink.clipClear()
            }
            SdpWire.Op.TEXT -> {
                val font = r.takeU8(); val x = r.takeU8(); val y = r.takeU8()
                if (!r.ok()) break
                if (font > SdpWire.MAX_FONT_ID || !checkCoord(x) || !checkCoord(y)) {
                    r.reject(); break
                }
                val color = decodeColor(r, pal) ?: break
                val al = r.takeU8(); val len = r.takeU8()
                if (!r.ok()) break
                if (al > SdpWire.Align.MAX) { r.reject(); break }
                val utf8 = r.takeBytes(len) ?: break
                if (execute && sink != null) sink.text(font, x, y, color, al, len, utf8)
            }
            SdpWire.Op.TEXT_BOX -> {
                val font = r.takeU8()
                val x = r.takeU8(); val y = r.takeU8(); val w = r.takeU8(); val h = r.takeU8()
                if (!r.ok()) break
                if (font > SdpWire.MAX_FONT_ID || !checkRect(x, y, w, h)) {
                    r.reject(); break
                }
                val color = decodeColor(r, pal) ?: break
                val al = r.takeU8(); val flags = r.takeU8(); val len = r.takeU8()
                if (!r.ok()) break
                if (al > SdpWire.Align.MAX || flags and SdpWire.TextBoxFlags.ALLOWED.inv() != 0) {
                    r.reject(); break
                }
                val utf8 = r.takeBytes(len) ?: break
                if (execute && sink != null) sink.textBox(font, x, y, w, h, color, al, flags, len, utf8)
            }
            SdpWire.Op.ICON -> {
                val atlas = r.takeU8(); val id = r.takeU16Le()
                val x = r.takeU8(); val y = r.takeU8()
                if (!r.ok()) break
                if (atlas > SdpWire.MAX_ATLAS_ID || id > SdpWire.MAX_ICON_ID ||
                    !checkCoord(x) || !checkCoord(y)
                ) {
                    r.reject(); break
                }
                val tint = decodeColor(r, pal) ?: break
                if (execute && sink != null) sink.icon(atlas, id, x, y, tint)
            }
            SdpWire.Op.IMAGE -> {
                val asset = r.takeU8(); val id = r.takeU16Le()
                val x = r.takeU8(); val y = r.takeU8()
                if (!r.ok()) break
                if (asset > SdpWire.MAX_ASSET_ID || id > SdpWire.MAX_IMAGE_ID ||
                    !checkCoord(x) || !checkCoord(y)
                ) {
                    r.reject(); break
                }
                if (execute && sink != null) sink.image(asset, id, x, y)
            }
            SdpWire.Op.PROGRESS_BAR -> {
                val x = r.takeU8(); val y = r.takeU8(); val w = r.takeU8(); val h = r.takeU8()
                val pct = r.takeU8()
                if (!r.ok()) break
                if (!checkRect(x, y, w, h) || pct > 100) { r.reject(); break }
                val fg = decodeColor(r, pal) ?: break
                val bg = decodeColor(r, pal) ?: break
                if (execute && sink != null) sink.progressBar(x, y, w, h, pct, fg, bg)
            }
            SdpWire.Op.PROGRESS_ARC -> {
                val cx = r.takeU8(); val cy = r.takeU8(); val rad = r.takeU8(); val pct = r.takeU8()
                if (!r.ok()) break
                if (!checkCoord(cx) || !checkCoord(cy) || pct > 100) { r.reject(); break }
                val fg = decodeColor(r, pal) ?: break
                val bg = decodeColor(r, pal) ?: break
                val width = r.takeU8()
                if (!r.ok()) break
                if (execute && sink != null) sink.progressArc(cx, cy, rad, pct, fg, bg, width)
            }
            SdpWire.Op.BEGIN_ELEM -> {
                val id = r.takeU16Le()
                val x = r.takeU8(); val y = r.takeU8(); val w = r.takeU8(); val h = r.takeU8()
                val flags = r.takeU8()
                if (!r.ok()) break
                if (!checkRect(x, y, w, h) || flags and SdpWire.ElemFlags.ALLOWED.inv() != 0) {
                    r.reject(); break
                }
                if (elemDepth >= SdpWire.MAX_ELEM_DEPTH) { r.reject(); break }
                elemDepth++
                if (execute && sink != null) sink.beginElem(id, x, y, w, h, flags)
            }
            SdpWire.Op.END_ELEM -> {
                if (elemDepth == 0) { r.reject(); break }
                elemDepth--
                if (execute && sink != null) sink.endElem()
            }
            SdpWire.Op.SCROLL_REGION -> {
                val y = r.takeU8(); val h = r.takeU8(); val contentH = r.takeU16Le()
                if (!r.ok()) break
                if (!checkCoord(y) || h == 0 || y + h > SdpWire.DISPLAY_SIZE) {
                    r.reject(); break
                }
                if (execute && sink != null) sink.scrollRegion(y, h, contentH)
            }
            SdpWire.Op.PATCH -> {
                val slot = r.takeU8()
                val x = r.takeU8(); val y = r.takeU8(); val w = r.takeU8(); val h = r.takeU8()
                val format = r.takeU8(); val encoding = r.takeU8()
                val len = r.takeU16Le()
                if (!r.ok()) break
                if (!checkRect(x, y, w, h) || format > SdpWire.PatchFormat.MAX ||
                    encoding > SdpWire.PatchEncoding.MAX || len > 4096
                ) {
                    r.reject(); break
                }
                val pdata = r.takeBytes(len) ?: break
                if (execute && sink != null) sink.patch(slot, x, y, w, h, format, encoding, len, pdata)
            }
            SdpWire.Op.PATCH_REF -> {
                val slot = r.takeU8(); val x = r.takeU8(); val y = r.takeU8()
                if (!r.ok()) break
                if (!checkCoord(x) || !checkCoord(y)) { r.reject(); break }
                if (execute && sink != null) sink.patchRef(slot, x, y)
            }
            SdpWire.Op.HAPTIC -> {
                val pattern = r.takeU8()
                if (!r.ok()) break
                if (pattern > SdpWire.HapticPattern.MAX) { r.reject(); break }
                if (execute && sink != null) sink.haptic(pattern)
            }
            SdpWire.Op.BACKLIGHT -> {
                val level = r.takeU8()
                if (!r.ok()) break
                if (level > 100) { r.reject(); break }
                if (execute && sink != null) sink.backlight(level)
            }
            SdpWire.Op.COMMIT -> {
                val flags = r.takeU8()
                if (!r.ok()) break
                if (flags and SdpWire.CommitFlags.ALLOWED.inv() != 0 || elemDepth != 0) {
                    r.reject(); break
                }
                out.sawCommit = true
                out.commitFlags = flags
                committed = true
                if (execute && sink != null) sink.commit(flags)
            }
            SdpWire.Op.RETAIN -> {
                val ttl = r.takeU16Le()
                if (!r.ok()) break
                if (execute && sink != null) sink.retain(ttl)
            }
            else -> {
                r.reject()
                break
            }
        }
    }

    if (out.status == SdpStatus.Ok) out.status = r.status
    out.bytesConsumed = r.pos
    return out
}
