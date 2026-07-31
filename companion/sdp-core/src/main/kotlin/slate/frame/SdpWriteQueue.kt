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
    private val lock = Any()
    private val seq = IntArray(8)
    private val pkts = ArrayDeque<ByteArray>()

    /**
     * Fragment [msg] and append all fragments contiguously.
     * Returns the number of fragments enqueued.
     */
    fun enqueueMessage(channel: Int, msg: ByteArray, extraFlags: Int = 0): Int =
        synchronized(lock) {
            val holder = intArrayOf(seq[channel])
            val fragments = SdpFrame.fragmentMessage(channel, msg, holder, extraFlags)
            seq[channel] = holder[0]
            pkts.addAll(fragments)
            fragments.size
        }

    fun poll(): ByteArray? = synchronized(lock) { pkts.removeFirstOrNull() }

    /** Drops queued packets; per-channel sequences keep counting. */
    fun clear() {
        synchronized(lock) { pkts.clear() }
    }

    fun isEmpty(): Boolean = synchronized(lock) { pkts.isEmpty() }
}
