package slate.frame

/**
 * SDP packet framing — matches firmware §4.2 / `sdp_frame.hpp`.
 * ATT payload at MTU 247 is 244 bytes.
 */
object SdpFrame {
    const val ATT_MTU_TARGET = 247
    const val ATT_PAYLOAD_MAX = 244
    const val MAX_MESSAGE_BYTES = 4096

    const val CHAN_CONTROL = 0
    const val CHAN_DISPLAY = 1
    const val CHAN_INPUT = 2
    const val CHAN_ASSET = 3
    const val CHAN_SYSTEM = 4
    const val CHAN_OTA = 5
    const val CHAN_RESERVED = 6
    const val CHAN_DIAG = 7

    const val FLAG_FIRST = 1 shl 0
    const val FLAG_LAST = 1 shl 1
    const val FLAG_ACK_REQ = 1 shl 2
    const val FLAG_URGENT = 1 shl 3

    fun packByte0(channel: Int, flags: Int): Int =
        ((channel and 0x07) shl 5) or (flags and 0x1F)

    fun encodeFragment(
        channel: Int,
        flags: Int,
        seq: Int,
        totalLen: Int,
        payload: ByteArray,
        payloadOffset: Int = 0,
        payloadLen: Int = payload.size,
    ): ByteArray {
        require(channel in 0..7)
        require(flags and 0x10 == 0) { "reserved flag bit4 must be zero" }
        val first = flags and FLAG_FIRST != 0
        val last = flags and FLAG_LAST != 0
        val needLen = first && !last
        val hdr = if (needLen) 4 else 2
        require(hdr + payloadLen <= ATT_PAYLOAD_MAX)
        val out = ByteArray(hdr + payloadLen)
        out[0] = packByte0(channel, flags).toByte()
        out[1] = (seq and 0xFF).toByte()
        if (needLen) {
            out[2] = (totalLen and 0xFF).toByte()
            out[3] = ((totalLen shr 8) and 0xFF).toByte()
        }
        if (payloadLen > 0) {
            System.arraycopy(payload, payloadOffset, out, hdr, payloadLen)
        }
        return out
    }

    /**
     * Fragment [msg] into ATT-sized frames. [seq] is updated (wraps at 256).
     * [extraFlags]: ACK_REQ / URGENT only.
     */
    fun fragmentMessage(
        channel: Int,
        msg: ByteArray,
        seq: IntArray, // single-element holder
        extraFlags: Int = 0,
    ): List<ByteArray> {
        require(msg.size <= MAX_MESSAGE_BYTES)
        require(seq.size == 1)
        val extra = extraFlags and 0x1C
        val singleCap = ATT_PAYLOAD_MAX - 2
        val firstMultiCap = ATT_PAYLOAD_MAX - 4
        val contCap = ATT_PAYLOAD_MAX - 2
        val out = ArrayList<ByteArray>()

        if (msg.size <= singleCap) {
            val flags = FLAG_FIRST or FLAG_LAST or extra
            out += encodeFragment(channel, flags, seq[0], msg.size, msg)
            seq[0] = (seq[0] + 1) and 0xFF
            return out
        }

        var offset = 0
        var isFirst = true
        while (offset < msg.size) {
            var flags = extra
            var cap = contCap
            if (isFirst) {
                flags = flags or FLAG_FIRST
                cap = firstMultiCap
            }
            var chunk = msg.size - offset
            if (chunk > cap) chunk = cap
            if (offset + chunk >= msg.size) {
                flags = flags or FLAG_LAST
            }
            out += encodeFragment(
                channel, flags, seq[0], msg.size, msg, offset, chunk,
            )
            seq[0] = (seq[0] + 1) and 0xFF
            offset += chunk
            isFirst = false
        }
        return out
    }
}
