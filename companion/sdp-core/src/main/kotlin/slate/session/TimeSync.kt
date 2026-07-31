package slate.session

/**
 * CONTROL channel wall-clock sync (firmware `sdp::control_op::TIME_SYNC` =
 * 0x20 + u32 LE epoch).
 * Not GATT CTS — that client is deferred.
 */
object TimeSync {
    const val TIME_SYNC_OP: Int = 0x20

    fun encodeUnix(epochSeconds: Long): ByteArray {
        val e = epochSeconds and 0xFFFF_FFFFL
        return byteArrayOf(
            TIME_SYNC_OP.toByte(),
            (e and 0xFF).toByte(),
            ((e shr 8) and 0xFF).toByte(),
            ((e shr 16) and 0xFF).toByte(),
            ((e shr 24) and 0xFF).toByte(),
        )
    }
}
