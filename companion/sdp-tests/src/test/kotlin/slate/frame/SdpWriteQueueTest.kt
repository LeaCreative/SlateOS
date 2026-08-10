package slate.frame

import java.util.concurrent.CountDownLatch
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * §4.2 single-in-flight: the encoder must never interleave fragments of
 * messages on different channels. Walks the emitted packet stream and fails
 * on any fragment that lands inside another channel's FIRST..LAST run.
 */
class SdpWriteQueueTest {

    /** Asserts contiguity, then reassembles and returns (channel, bytes) per message. */
    private fun drainAndVerify(q: SdpWriteQueue): List<Pair<Int, ByteArray>> {
        val reasm = SdpReassembler(diagAllowed = true)
        val messages = ArrayList<Pair<Int, ByteArray>>()
        var inFlight = -1
        while (true) {
            val entry = q.poll() ?: break
            val pkt = entry.bytes
            val channel = (pkt[0].toInt() ushr 5) and 0x07
            val flags = pkt[0].toInt() and 0x1F
            val first = flags and SdpFrame.FLAG_FIRST != 0
            val last = flags and SdpFrame.FLAG_LAST != 0
            if (first) {
                assertEquals(-1, inFlight, "FIRST on ch=$channel while ch=$inFlight in flight")
            } else {
                assertEquals(inFlight, channel, "continuation strayed across channels")
            }
            inFlight = if (last) -1 else channel
            val st = reasm.ingest(pkt)
            if (st == SdpReassembler.Status.Ok) {
                messages += reasm.messageChannel() to reasm.message()
            } else {
                assertEquals(SdpReassembler.Status.NeedMore, st)
            }
        }
        assertEquals(-1, inFlight, "stream ended mid-message")
        assertEquals(0, reasm.preemptDrops)
        return messages
    }

    @Test
    fun sequentialMessagesStayContiguous() {
        val q = SdpWriteQueue()
        val display = ByteArray(1000) { 0xD1.toByte() }
        val ota = ByteArray(700) { 0x0A }
        val control = byteArrayOf(1, 2, 3)
        q.enqueueMessage(SdpFrame.CHAN_DISPLAY, display)
        q.enqueueMessage(SdpFrame.CHAN_OTA, ota)
        q.enqueueMessage(SdpFrame.CHAN_CONTROL, control)

        val messages = drainAndVerify(q)
        assertEquals(3, messages.size)
        assertEquals(SdpFrame.CHAN_DISPLAY, messages[0].first)
        assertTrue(display.contentEquals(messages[0].second))
        assertEquals(SdpFrame.CHAN_OTA, messages[1].first)
        assertTrue(ota.contentEquals(messages[1].second))
        assertEquals(SdpFrame.CHAN_CONTROL, messages[2].first)
        assertTrue(control.contentEquals(messages[2].second))
    }

    @Test
    fun concurrentEnqueuesNeverInterleave() {
        val q = SdpWriteQueue()
        val channels = intArrayOf(
            SdpFrame.CHAN_DISPLAY, SdpFrame.CHAN_ASSET, SdpFrame.CHAN_OTA,
        )
        val perThread = 20
        val start = CountDownLatch(1)
        val threads = channels.map { ch ->
            Thread {
                start.await()
                repeat(perThread) { i ->
                    // Multi-fragment (>242 B) so interleaving is possible at all.
                    q.enqueueMessage(ch, ByteArray(300 + i) { (ch * 16 + 1).toByte() })
                }
            }
        }
        threads.forEach { it.start() }
        start.countDown()
        threads.forEach { it.join() }

        val messages = drainAndVerify(q)
        assertEquals(channels.size * perThread, messages.size)
        for ((ch, bytes) in messages) {
            val expected = (ch * 16 + 1).toByte()
            assertTrue(bytes.all { it == expected }, "cross-contaminated message on ch=$ch")
        }
    }

    @Test
    fun perChannelSequencesMatchChannelSeqBehaviour() {
        // Byte-exact framing parity with direct fragmentMessage calls,
        // including seq continuation across messages on the same channel.
        val q = SdpWriteQueue()
        val m1 = ByteArray(500) { it.toByte() }
        val m2 = ByteArray(10) { (it + 1).toByte() }
        q.enqueueMessage(SdpFrame.CHAN_DISPLAY, m1)
        q.enqueueMessage(SdpFrame.CHAN_DISPLAY, m2)

        val seq = intArrayOf(0)
        val expected =
            SdpFrame.fragmentMessage(SdpFrame.CHAN_DISPLAY, m1, seq) +
                SdpFrame.fragmentMessage(SdpFrame.CHAN_DISPLAY, m2, seq)
        for (e in expected) {
            val got = q.poll()
            assertTrue(got != null && e.contentEquals(got.bytes), "fragment mismatch")
        }
        assertNull(q.poll())
    }

    @Test
    fun clearDropsPacketsButKeepsSequence() {
        val q = SdpWriteQueue()
        q.enqueueMessage(SdpFrame.CHAN_DISPLAY, byteArrayOf(1))
        q.clear()
        assertNull(q.poll())
        q.enqueueMessage(SdpFrame.CHAN_DISPLAY, byteArrayOf(2))
        val pkt = q.poll()!!.bytes
        assertEquals(1, pkt[1].toInt() and 0xFF) // seq continued past cleared pkt
    }

    @Test
    fun endsMessageMarksOnlyTheFinalFragment() {
        // The write pump needs message boundaries to pace sends: the watch's
        // AppInbox holds one message and silently drops anything arriving
        // before the app task drains it (N-28).
        val q = SdpWriteQueue()
        val big = ByteArray(600) { it.toByte() }
        val n = q.enqueueMessage(SdpFrame.CHAN_DISPLAY, big)
        assertTrue(n > 1, "expected a multi-fragment message, got $n")
        val pkts = generateSequence { q.poll() }.toList()
        assertEquals(n, pkts.size)
        pkts.dropLast(1).forEach { assertTrue(!it.endsMessage, "mid fragment marked as end") }
        assertTrue(pkts.last().endsMessage, "final fragment not marked")
        pkts.forEach { assertEquals(SdpFrame.CHAN_DISPLAY, it.channel) }
    }

    @Test
    fun displayJumpsAheadOfQueuedSystem() {
        val q = SdpWriteQueue()
        q.enqueueMessage(SdpFrame.CHAN_SYSTEM, ByteArray(20) { 0x04 })
        q.enqueueMessage(SdpFrame.CHAN_SYSTEM, ByteArray(20) { 0x05 })
        q.enqueueMessage(SdpFrame.CHAN_DISPLAY, ByteArray(30) { 0xD1.toByte() })
        q.enqueueMessage(SdpFrame.CHAN_CONTROL, byteArrayOf(0x01))

        val first = q.poll()!!
        assertEquals(SdpFrame.CHAN_DISPLAY, first.channel)
        val second = q.poll()!!
        assertEquals(SdpFrame.CHAN_CONTROL, second.channel)
        assertEquals(SdpFrame.CHAN_SYSTEM, q.poll()!!.channel)
        assertEquals(SdpFrame.CHAN_SYSTEM, q.poll()!!.channel)
        assertNull(q.poll())
    }

    @Test
    fun requeueFrontRestoresPacket() {
        val q = SdpWriteQueue()
        q.enqueueMessage(SdpFrame.CHAN_CONTROL, byteArrayOf(0x0A))
        q.enqueueMessage(SdpFrame.CHAN_SYSTEM, byteArrayOf(0x0B))
        val dropped = q.poll()!!
        q.requeueFront(dropped)
        assertEquals(SdpFrame.CHAN_CONTROL, q.poll()!!.channel)
        assertEquals(SdpFrame.CHAN_SYSTEM, q.poll()!!.channel)
    }
}
