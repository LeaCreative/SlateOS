package slate.input

import slate.generated.SdpWire
import slate.host.HostInbound

/** Decode SDP input events (channel 2, §4.4). */
object InputEventDecoder {
    fun decode(msg: ByteArray): HostInbound.Input? {
        if (msg.isEmpty()) return null
        return when (val op = msg[0].toInt() and 0xFF) {
            SdpWire.InputOp.TAP,
            SdpWire.InputOp.LONG_PRESS,
            -> {
                if (msg.size < 4) return null
                val elemId = u16Le(msg, 1)
                val x = if (msg.size > 3) msg[3].toInt() and 0xFF else 0
                val y = if (msg.size > 4) msg[4].toInt() and 0xFF else 0
                HostInbound.Input(op = op, elemId = elemId, x = x, y = y)
            }
            SdpWire.InputOp.SWIPE -> {
                if (msg.size < 2) return null
                HostInbound.Input(op = op, dir = msg[1].toInt() and 0xFF)
            }
            SdpWire.InputOp.BUTTON -> {
                if (msg.size < 2) return null
                HostInbound.Input(op = op, action = msg[1].toInt() and 0xFF)
            }
            SdpWire.InputOp.SCROLL_POS -> {
                if (msg.size < 4) return null
                val region = msg[1].toInt() and 0xFF
                val offset = u16Le(msg, 2)
                HostInbound.Input(op = op, elemId = region, distance = offset)
            }
            SdpWire.InputOp.BACK,
            SdpWire.InputOp.SESSION_END,
            -> HostInbound.Input(op = op, reason = if (msg.size > 1) msg[1].toInt() and 0xFF else 0)
            SdpWire.InputOp.MULTI_TAP -> {
                if (msg.size < 6) return null
                val count = msg[1].toInt() and 0xFF
                val elemId = u16Le(msg, 2)
                val x = msg[4].toInt() and 0xFF
                val y = msg[5].toInt() and 0xFF
                HostInbound.Input(op = op, count = count, elemId = elemId, x = x, y = y)
            }
            SdpWire.InputOp.EDGE_SWIPE -> {
                if (msg.size < 4) return null
                HostInbound.Input(
                    op = op,
                    edge = msg[1].toInt() and 0xFF,
                    dir = msg[2].toInt() and 0xFF,
                    distance = msg[3].toInt() and 0xFF,
                )
            }
            SdpWire.InputOp.TOUCH_DOWN,
            SdpWire.InputOp.TOUCH_UP,
            -> {
                if (msg.size < 4) return null
                val elemId = u16Le(msg, 1)
                val x = msg[3].toInt() and 0xFF
                val y = if (msg.size > 4) msg[4].toInt() and 0xFF else 0
                HostInbound.Input(op = op, elemId = elemId, x = x, y = y)
            }
            else -> null
        }
    }

    private fun u16Le(buf: ByteArray, off: Int): Int =
        (buf[off].toInt() and 0xFF) or ((buf[off + 1].toInt() and 0xFF) shl 8)
}
