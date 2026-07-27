package slate.app.link

import android.util.Log
import slate.frame.SdpFrame
import slate.uuid.SlateUuids
import java.util.UUID

object LinkLog {
    const val TAG = "SlateLink"
    fun i(msg: String) = Log.i(TAG, msg)
    fun w(msg: String) = Log.w(TAG, msg)
    fun e(msg: String, t: Throwable? = null) = Log.e(TAG, msg, t)
}

data class LinkMetrics(
    val connected: Boolean = false,
    val deviceAddress: String = "",
    val attMtu: Int = 23,
    val phyTx: String = "—",
    val phyRx: String = "—",
    val intervalMs: Double? = null,
    val rttMs: Double? = null,
    val lastError: String = "",
    val lastPushAt: Long = 0L,
    val notes: String = "",
)

object SlateGattIds {
    val SERVICE: UUID = SlateUuids.SERVICE
    val RX: UUID = SlateUuids.RX
    val TX: UUID = SlateUuids.TX
    val STATUS: UUID = SlateUuids.STATUS
    val CCCD: UUID = SlateUuids.CCCD
}

/** Per-channel TX sequence (matches firmware). */
class ChannelSeq {
    private val seq = IntArray(8)
    fun nextFragments(channel: Int, msg: ByteArray): List<ByteArray> {
        val holder = intArrayOf(seq[channel])
        val pkts = SdpFrame.fragmentMessage(channel, msg, holder)
        seq[channel] = holder[0]
        return pkts
    }
}
