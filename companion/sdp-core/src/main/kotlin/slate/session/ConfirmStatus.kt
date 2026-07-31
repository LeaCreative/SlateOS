package slate.session

import slate.generated.SdpWire

/**
 * CONTROL channel trial-image confirm status (firmware `CONFIRM_STATUS` =
 * 0xE1 + needs_confirm:u8 + dwell_ms_remaining:u32 LE).
 *
 * Confirm semantics stay on the watch (`kConfirmDwellMs`); this is visibility
 * only. Phone → watch request is a single-byte `CONFIRM_STATUS_REQUEST` (0xE0).
 */
object ConfirmStatus {
    const val REQUEST_OP: Int = SdpWire.ControlOp.CONFIRM_STATUS_REQUEST
    const val STATUS_OP: Int = SdpWire.ControlOp.CONFIRM_STATUS

    data class Snapshot(
        val needsConfirm: Boolean,
        val dwellMsRemaining: Long,
    )

    fun encodeRequest(): ByteArray = byteArrayOf(REQUEST_OP.toByte())

    fun parse(msg: ByteArray): Snapshot? {
        if (msg.size < 6) return null
        if (msg[0].toInt() and 0xFF != STATUS_OP) return null
        val needs = (msg[1].toInt() and 0xFF) != 0
        val rem = (msg[2].toLong() and 0xFF) or
            ((msg[3].toLong() and 0xFF) shl 8) or
            ((msg[4].toLong() and 0xFF) shl 16) or
            ((msg[5].toLong() and 0xFF) shl 24)
        return Snapshot(needsConfirm = needs, dwellMsRemaining = rem)
    }
}
