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
 *
 * Within the FIFO of *messages*, CONTROL and DISPLAY are inserted ahead of
 * SYSTEM/ASSET so a launcher push is not trapped behind a notification dump
 * (operator log: DISPLAY enqueued at :30.158, ATT write of that list at
 * :45.908 — fifteen seconds of ch=4 ahead of it).
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
     * Fragment [msg] and insert all fragments contiguously.
     *
     * High-priority channels (CONTROL, DISPLAY, OTA) are placed before any
     * already-queued low-priority message, without splitting an in-flight
     * multi-fragment message.
     *
     * Returns the number of fragments enqueued.
     */
    fun enqueueMessage(channel: Int, msg: ByteArray, extraFlags: Int = 0): Int =
        synchronized(lock) {
            val holder = intArrayOf(seq[channel])
            val fragments = SdpFrame.fragmentMessage(channel, msg, holder, extraFlags)
            seq[channel] = holder[0]
            val batch = fragments.mapIndexed { i, f ->
                Pkt(f, channel, endsMessage = i == fragments.lastIndex)
            }
            if (isHighPriority(channel)) {
                insertHighPriority(batch)
            } else {
                pkts.addAll(batch)
            }
            fragments.size
        }

    /**
     * Put a previously polled packet back at the head (write failed before
     * the controller accepted it). Must not invent a new sequence number.
     */
    fun requeueFront(pkt: Pkt) = synchronized(lock) {
        pkts.addFirst(pkt)
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

    /** True when any queued fragment is on [channel] (used to skip pacing ahead of OTA). */
    fun containsChannel(channel: Int): Boolean = synchronized(lock) {
        pkts.any { it.channel == channel }
    }

    private fun isHighPriority(channel: Int): Boolean =
        channel == SdpFrame.CHAN_CONTROL ||
            channel == SdpFrame.CHAN_DISPLAY ||
            channel == SdpFrame.CHAN_OTA

    /**
     * Insert [batch] before the first low-priority message, after any
     * high-priority run already at the head (preserves message contiguity).
     */
    private fun insertHighPriority(batch: List<Pkt>) {
        if (pkts.isEmpty()) {
            pkts.addAll(batch)
            return
        }
        // Skip leading high-priority fragments (complete messages + any
        // partial high-priority message still being built at the head).
        var i = 0
        val snapshot = pkts.toList()
        while (i < snapshot.size && isHighPriority(snapshot[i].channel)) {
            ++i
        }
        // Walk back to a message boundary: never insert mid-message.
        while (i > 0 && !snapshot[i - 1].endsMessage) {
            --i
        }
        if (i >= pkts.size) {
            pkts.addAll(batch)
            return
        }
        // Rebuild: [0, i) + batch + [i, end)
        val tail = ArrayList<Pkt>()
        repeat(pkts.size - i) { tail.add(pkts.removeLast()) }
        tail.reverse()
        pkts.addAll(batch)
        pkts.addAll(tail)
    }
}
