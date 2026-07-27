package slate.diag

/**
 * Channel-7 DIAG benchmark protocol — mirrors firmware `sdp_diag.hpp`.
 * Debug builds only on the watch.
 */
object SdpDiag {
    const val OP_PING_REQ: Int = 0x01
    const val OP_THRU_START: Int = 0x02
    const val OP_THRU_DATA: Int = 0x03
    const val OP_RTT_REQ: Int = 0x04
    const val OP_MBUF_REQ: Int = 0x05
    const val OP_RENDER_REQ: Int = 0x06

    const val OP_PING_RSP: Int = 0x81
    const val OP_THRU_RESULT: Int = 0x82
    const val OP_RTT_RSP: Int = 0x84
    const val OP_MBUF_RSP: Int = 0x85
    const val OP_RENDER_RSP: Int = 0x86

    fun u16Le(v: Int): ByteArray = byteArrayOf(
        (v and 0xFF).toByte(),
        ((v shr 8) and 0xFF).toByte(),
    )

    fun u32Le(v: Long): ByteArray = byteArrayOf(
        (v and 0xFF).toByte(),
        ((v shr 8) and 0xFF).toByte(),
        ((v shr 16) and 0xFF).toByte(),
        ((v shr 24) and 0xFF).toByte(),
    )

    fun u64Le(v: Long): ByteArray {
        val out = ByteArray(8)
        var x = v
        for (i in 0 until 8) {
            out[i] = (x and 0xFF).toByte()
            x = x ushr 8
        }
        return out
    }

    fun getU16(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

    fun getU32(b: ByteArray, off: Int): Long =
        (b[off].toLong() and 0xFF) or
            ((b[off + 1].toLong() and 0xFF) shl 8) or
            ((b[off + 2].toLong() and 0xFF) shl 16) or
            ((b[off + 3].toLong() and 0xFF) shl 24)

    fun pingReq(token: ByteArray): ByteArray = byteArrayOf(OP_PING_REQ.toByte()) + token

    fun rttReq(token: ByteArray): ByteArray = byteArrayOf(OP_RTT_REQ.toByte()) + token

    fun thruStart(expectedBytes: Int, firstChunk: ByteArray = ByteArray(0)): ByteArray =
        byteArrayOf(OP_THRU_START.toByte()) + u32Le(expectedBytes.toLong()) + firstChunk

    fun thruData(chunk: ByteArray): ByteArray =
        byteArrayOf(OP_THRU_DATA.toByte()) + chunk

    fun mbufReq(): ByteArray = byteArrayOf(OP_MBUF_REQ.toByte())

    fun renderReq(displayList: ByteArray): ByteArray =
        byteArrayOf(OP_RENDER_REQ.toByte()) + displayList

    data class ThruResult(
        val elapsedUs: Long,
        val bytes: Long,
        val kbps: Double,
        val status: Int,
    )

    data class RttResult(
        val token: ByteArray,
        val watchUs: Long,
    )

    data class MbufResult(
        val peakUsed: Int,
        val blockCount: Int,
        val blockSize: Int,
        val freeNow: Int,
    )

    data class RenderResult(
        val parseUs: Long,
        val renderUs: Long,
        val ops: Long,
        val status: Int,
    ) {
        val totalUs: Long get() = parseUs + renderUs
    }

    fun parseThruResult(msg: ByteArray): ThruResult? {
        if (msg.size < 14 || (msg[0].toInt() and 0xFF) != OP_THRU_RESULT) return null
        val elapsed = getU32(msg, 1)
        val bytes = getU32(msg, 5)
        val kbpsX100 = getU32(msg, 9)
        return ThruResult(elapsed, bytes, kbpsX100 / 100.0, msg[13].toInt() and 0xFF)
    }

    fun parseRttResult(msg: ByteArray): RttResult? {
        if (msg.size < 6 || (msg[0].toInt() and 0xFF) != OP_RTT_RSP) return null
        val token = msg.copyOfRange(1, msg.size - 4)
        val watchUs = getU32(msg, msg.size - 4)
        return RttResult(token, watchUs)
    }

    fun parseMbufResult(msg: ByteArray): MbufResult? {
        if (msg.size < 9 || (msg[0].toInt() and 0xFF) != OP_MBUF_RSP) return null
        return MbufResult(
            peakUsed = getU16(msg, 1),
            blockCount = getU16(msg, 3),
            blockSize = getU16(msg, 5),
            freeNow = getU16(msg, 7),
        )
    }

    fun parseRenderResult(msg: ByteArray): RenderResult? {
        if (msg.size < 14 || (msg[0].toInt() and 0xFF) != OP_RENDER_RSP) return null
        return RenderResult(
            parseUs = getU32(msg, 1),
            renderUs = getU32(msg, 5),
            ops = getU32(msg, 9),
            status = msg[13].toInt() and 0xFF,
        )
    }
}
