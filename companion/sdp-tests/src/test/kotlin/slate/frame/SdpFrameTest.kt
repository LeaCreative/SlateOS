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

    // §4.2 single-in-flight mirror of firmware Reassembler behaviour.
    @Test
    fun firstOnOtherChannelAbortsInFlightReassembly() {
        val a = SdpFrame.fragmentMessage(
            SdpFrame.CHAN_DISPLAY, ByteArray(400) { 0xAA.toByte() }, intArrayOf(0),
        )
        val b = SdpFrame.fragmentMessage(
            SdpFrame.CHAN_OTA, ByteArray(400) { 0xBB.toByte() }, intArrayOf(0),
        )
        val r = SdpReassembler(diagAllowed = true)
        assertEquals(SdpReassembler.Status.NeedMore, r.ingest(a[0]))
        // B preempts mid-A.
        assertEquals(SdpReassembler.Status.NeedMore, r.ingest(b[0]))
        assertEquals(1, r.preemptDrops)
        var last = SdpReassembler.Status.Dropped
        for (i in 1 until b.size) last = r.ingest(b[i])
        assertEquals(SdpReassembler.Status.Ok, last)
        assertEquals(SdpFrame.CHAN_OTA, r.messageChannel())
        assertTrue(r.message().all { it == 0xBB.toByte() }, "B contaminated by A")
        // A's leftover continuation is dropped, not stitched into stale state.
        assertEquals(SdpReassembler.Status.Dropped, r.ingest(a[1]))
    }

    @Test
    fun singleFragmentMessageAbortsInFlightReassembly() {
        val a = SdpFrame.fragmentMessage(
            SdpFrame.CHAN_DISPLAY, ByteArray(400) { 0xAA.toByte() }, intArrayOf(0),
        )
        val ctl = SdpFrame.fragmentMessage(
            SdpFrame.CHAN_CONTROL, byteArrayOf(0x20, 1, 2, 3), intArrayOf(0),
        )
        assertEquals(1, ctl.size)
        val r = SdpReassembler(diagAllowed = true)
        assertEquals(SdpReassembler.Status.NeedMore, r.ingest(a[0]))
        assertEquals(SdpReassembler.Status.Ok, r.ingest(ctl[0]))
        assertTrue(byteArrayOf(0x20, 1, 2, 3).contentEquals(r.message()))
        assertEquals(1, r.preemptDrops)
        assertEquals(SdpReassembler.Status.Dropped, r.ingest(a[1]))
    }
}
