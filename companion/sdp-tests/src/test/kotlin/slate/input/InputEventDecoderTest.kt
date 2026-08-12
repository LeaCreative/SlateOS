package slate.input

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import slate.generated.SdpWire
import slate.host.HostInbound

class InputEventDecoderTest {
    @Test
    fun tap() {
        val msg = byteArrayOf(
            SdpWire.InputOp.TAP.toByte(),
            0x2A, 0x00, // elem 42
            10, 20,
        )
        val ev = InputEventDecoder.decode(msg) as HostInbound.Input
        assertEquals(SdpWire.InputOp.TAP, ev.op)
        assertEquals(42, ev.elemId)
        assertEquals(10, ev.x)
        assertEquals(20, ev.y)
    }

    @Test
    fun swipe() {
        val ev = InputEventDecoder.decode(
            byteArrayOf(SdpWire.InputOp.SWIPE.toByte(), 3),
        ) as HostInbound.Input
        assertEquals(3, ev.dir)
    }

    @Test
    fun backAndSessionEnd() {
        val back = InputEventDecoder.decode(byteArrayOf(SdpWire.InputOp.BACK.toByte(), 1))!!
        assertEquals(SdpWire.InputOp.BACK, back.op)
        assertEquals(1, back.reason)
        val end = InputEventDecoder.decode(byteArrayOf(SdpWire.InputOp.SESSION_END.toByte()))!!
        assertEquals(SdpWire.InputOp.SESSION_END, end.op)
        assertEquals(0, end.reason)
    }

    @Test
    fun multiTap() {
        val msg = byteArrayOf(
            SdpWire.InputOp.MULTI_TAP.toByte(),
            3,
            0x01, 0x00,
            5, 6,
        )
        val ev = InputEventDecoder.decode(msg)!!
        assertEquals(3, ev.count)
        assertEquals(1, ev.elemId)
        assertEquals(5, ev.x)
        assertEquals(6, ev.y)
    }

    @Test
    fun edgeSwipe() {
        val ev = InputEventDecoder.decode(
            byteArrayOf(SdpWire.InputOp.EDGE_SWIPE.toByte(), 1, 2, 40),
        )!!
        assertEquals(1, ev.edge)
        assertEquals(2, ev.dir)
        assertEquals(40, ev.distance)
    }

    @Test
    fun touchDownUp() {
        val down = InputEventDecoder.decode(
            byteArrayOf(SdpWire.InputOp.TOUCH_DOWN.toByte(), 0x10, 0x00, 7, 8),
        )!!
        assertEquals(SdpWire.InputOp.TOUCH_DOWN, down.op)
        assertEquals(16, down.elemId)
        assertEquals(7, down.x)
        assertEquals(8, down.y)
        val up = InputEventDecoder.decode(
            byteArrayOf(SdpWire.InputOp.TOUCH_UP.toByte(), 0x10, 0x00, 7),
        )
        assertNotNull(up)
        assertEquals(0, up.y)
    }

    @Test
    fun scrollPos() {
        val ev = InputEventDecoder.decode(
            byteArrayOf(
                SdpWire.InputOp.SCROLL_POS.toByte(),
                0,
                0x30, 0x00, // offset 48
            ),
        )!!
        assertEquals(SdpWire.InputOp.SCROLL_POS, ev.op)
        assertEquals(0, ev.elemId)
        assertEquals(48, ev.distance)
    }

    @Test
    fun truncatedReturnsNull() {
        assertNull(InputEventDecoder.decode(byteArrayOf()))
        assertNull(InputEventDecoder.decode(byteArrayOf(SdpWire.InputOp.TAP.toByte(), 1)))
        assertNull(InputEventDecoder.decode(byteArrayOf(SdpWire.InputOp.MULTI_TAP.toByte(), 1, 2)))
        assertNull(InputEventDecoder.decode(byteArrayOf(0x7F)))
    }
}
