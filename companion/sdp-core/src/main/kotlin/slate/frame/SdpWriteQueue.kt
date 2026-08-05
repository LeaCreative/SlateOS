package slate.frame

/**
 * Thread-safe frame write queue enforcing the §4.2 single-in-flight rule.
 *
 * The watch reassembles all channels into one shared buffer: a FIRST fragment
 * on any channel aborts an in-flight multi-fragment message on another
 * channel (reject-and-resync, drop counted). Interleaving is therefore a
 * protocol violation on our side. This queue owns the per-channel TX sequence
 * and enqueues every fragment of a message in one atomic step, so fragments
 * of concurrent messages can never interleave on the link no matter which
 * threads call [enqueueMessage].
 */
class SdpWriteQueue {
    /**
     * One fragment, tagged with its channel and whether it closes a message.
     *
     * The sender must know where messages end: the watch's AppInbox holds a
     * single message and gates ingest while it is busy, so a second message
     * arriving before the app task drains (~20 ms) is dropped with no
     * retransmit — these are writes without response. Fragment-level queueing
     * alone cannot express "wait before the next message" (N-28).
     */
    data class Pkt(val bytes: ByteArray, val channel: Int, val endsMessage: Boolean) {
        override fun equals(other: Any?): Boolean =
            other is Pkt && channel == other.channel &&
                endsMessage == other.endsMessage && bytes.contentEquals(other.bytes)

        override fun hashCode(): Int =
            (bytes.contentHashCode() * 31 + channel) * 31 + endsMessage.hashCode()
    }

    private val lock = Any()
    private val seq = IntArray(8)
    private val pkts = ArrayDeque<Pkt>()

    /**
     * Fragment [msg] and append all fragments contiguously.
     * Returns the number of fragments enqueued.
     */
    fun enqueueMessage(channel: Int, msg: ByteArray, extraFlags: Int = 0): Int =
        synchronized(lock) {
            val holder = intArrayOf(seq[channel])
            val fragments = SdpFrame.fragmentMessage(channel, msg, holder, extraFlags)
            seq[channel] = holder[0]
            fragments.forEachIndexed { i, f ->
                pkts.add(Pkt(f, channel, endsMessage = i == fragments.lastIndex))
            }
            fragments.size
        }

    fun poll(): Pkt? = synchronized(lock) { pkts.removeFirstOrNull() }

    /** Drops queued packets; per-channel sequences keep counting. */
    fun clear() {
        synchronized(lock) { pkts.clear() }
    }

    /**
     * Drop queued packets **and** restart every channel sequence at zero.
     *
     * Call on connect. The watch's reassembler resets its expected sequence
     * whenever it reboots, but this queue lives for the lifetime of the GATT
     * client, so its counters kept climbing across reconnects. Once the two
     * diverged the watch rejected every frame — visible as a rising frame-drop
     * count with nothing reaching the session (N-32).
     */
    fun reset() {
        synchronized(lock) {
            pkts.clear()
            java.util.Arrays.fill(seq, 0)
        }
    }

    fun isEmpty(): Boolean = synchronized(lock) { pkts.isEmpty() }
}
