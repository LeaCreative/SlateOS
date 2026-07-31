package slate.frame

/**
 * Per-channel SDP frame reassembler — mirrors firmware `sdp::frame::Reassembler`
 * for the subset needed by the companion (DIAG TX notifies).
 *
 * Mirrors the §4.2 single-in-flight rule: all channels share one buffer, so a
 * FIRST on any channel aborts an in-flight message on another channel
 * (counted in [preemptDrops]) rather than letting it be silently corrupted.
 */
class SdpReassembler(
    private val diagAllowed: Boolean = true,
) {
    enum class Status { Ok, NeedMore, Dropped, ChannelReject }

    private data class ChanState(
        var active: Boolean = false,
        var expectSeq: Int = 0,
        var totalLen: Int = 0,
        var filled: Int = 0,
        var sawFirst: Boolean = false,
    )

    private val ch = Array(8) { ChanState() }
    private val buf = ByteArray(SdpFrame.MAX_MESSAGE_BYTES)
    private var msgLen = 0
    private var msgChannel = 0

    /** In-flight messages abandoned because a FIRST arrived on another channel. */
    var preemptDrops = 0
        private set

    fun reset() {
        for (c in ch) {
            c.active = false
            c.filled = 0
            c.sawFirst = false
        }
        msgLen = 0
    }

    fun message(): ByteArray = buf.copyOf(msgLen)
    fun messageLen(): Int = msgLen
    fun messageChannel(): Int = msgChannel

    fun ingest(pkt: ByteArray): Status {
        if (pkt.size < 2) return Status.Dropped
        val channel = (pkt[0].toInt() ushr 5) and 0x07
        val flags = pkt[0].toInt() and 0x1F
        val seq = pkt[1].toInt() and 0xFF
        if (channel == SdpFrame.CHAN_DIAG && !diagAllowed) return Status.ChannelReject
        if (channel == SdpFrame.CHAN_RESERVED && !diagAllowed) return Status.ChannelReject

        val first = flags and SdpFrame.FLAG_FIRST != 0
        val last = flags and SdpFrame.FLAG_LAST != 0
        val needLen = first && !last
        val hdr = if (needLen) 4 else 2
        if (pkt.size < hdr) return drop(channel)

        val st = ch[channel]
        if (first) {
            // Single-in-flight: abort in-flight reassembly on other channels
            // before this message overwrites the shared buffer.
            for (i in ch.indices) {
                if (i != channel && ch[i].active) {
                    drop(i)
                    preemptDrops++
                }
            }
            st.active = true
            st.expectSeq = seq
            st.filled = 0
            st.sawFirst = true
            st.totalLen = if (needLen) {
                (pkt[2].toInt() and 0xFF) or ((pkt[3].toInt() and 0xFF) shl 8)
            } else {
                pkt.size - hdr
            }
            if (st.totalLen > SdpFrame.MAX_MESSAGE_BYTES) return drop(channel)
        } else {
            if (!st.active || !st.sawFirst) return drop(channel)
            if (seq != st.expectSeq) return drop(channel)
        }
        if (seq != st.expectSeq) return drop(channel)

        val payloadLen = pkt.size - hdr
        if (st.filled + payloadLen > st.totalLen) return drop(channel)
        System.arraycopy(pkt, hdr, buf, st.filled, payloadLen)
        st.filled += payloadLen
        st.expectSeq = (st.expectSeq + 1) and 0xFF

        if (!last) return Status.NeedMore
        if (st.filled != st.totalLen) return drop(channel)
        msgLen = st.filled
        msgChannel = channel
        st.active = false
        return Status.Ok
    }

    private fun drop(channel: Int): Status {
        ch[channel].active = false
        ch[channel].filled = 0
        ch[channel].sawFirst = false
        return Status.Dropped
    }
}
