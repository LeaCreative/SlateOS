package slate.frame

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SdpFrameTest {
    @Test
    fun singleFragmentRoundTripShape() {
        val msg = byteArrayOf(1, 2, 3, 4)
        val seq = intArrayOf(0)
        val pkts = SdpFrame.fragmentMessage(SdpFrame.CHAN_DISPLAY, msg, seq)
        assertEquals(1, pkts.size)
        assertEquals(1, seq[0])
        val p = pkts[0]
        assertEquals(SdpFrame.packByte0(1, SdpFrame.FLAG_FIRST or SdpFrame.FLAG_LAST), p[0].toInt() and 0xFF)
        assertEquals(0, p[1].toInt() and 0xFF)
        assertEquals(4, p.size - 2)
    }

    @Test
    fun multiFragmentIncludesLengthOnFirst() {
        val msg = ByteArray(500) { it.toByte() }
        val seq = intArrayOf(0)
        val pkts = SdpFrame.fragmentMessage(SdpFrame.CHAN_DISPLAY, msg, seq)
        assertTrue(pkts.size >= 2)
        val first = pkts[0]
        assertTrue(first.size >= 4)
        val total = (first[2].toInt() and 0xFF) or ((first[3].toInt() and 0xFF) shl 8)
        assertEquals(500, total)
        assertTrue(first[0].toInt() and SdpFrame.FLAG_FIRST != 0)
        assertTrue(first[0].toInt() and SdpFrame.FLAG_LAST == 0)
        assertTrue(pkts.last()[0].toInt() and SdpFrame.FLAG_LAST != 0)
    }
}
