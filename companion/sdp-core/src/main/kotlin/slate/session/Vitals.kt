package slate.session

import slate.generated.SdpWire

/**
 * CONTROL `VITALS` = 0xE2 — watch → phone day steps + last BPM.
 *
 * Wire: `[op][steps:u32 LE][bpm:u8][pad:u8]` (7 bytes).
 */
object Vitals {
    const val OP: Int = SdpWire.ControlOp.VITALS
    const val PAYLOAD_LEN: Int = 7

    data class Snapshot(
        val steps: Long,
        val bpm: Int,
    )

    fun encode(steps: Long, bpm: Int): ByteArray {
        val s = steps.coerceIn(0L, 0xFFFF_FFFFL)
        val b = bpm.coerceIn(0, 255)
        return byteArrayOf(
            OP.toByte(),
            (s and 0xFF).toByte(),
            ((s shr 8) and 0xFF).toByte(),
            ((s shr 16) and 0xFF).toByte(),
            ((s shr 24) and 0xFF).toByte(),
            b.toByte(),
            0,
        )
    }

    fun parse(msg: ByteArray): Snapshot? {
        if (msg.size < PAYLOAD_LEN) return null
        if ((msg[0].toInt() and 0xFF) != OP) return null
        val steps = (msg[1].toLong() and 0xFF) or
            ((msg[2].toLong() and 0xFF) shl 8) or
            ((msg[3].toLong() and 0xFF) shl 16) or
            ((msg[4].toLong() and 0xFF) shl 24)
        val bpm = msg[5].toInt() and 0xFF
        return Snapshot(steps = steps, bpm = bpm)
    }
}
