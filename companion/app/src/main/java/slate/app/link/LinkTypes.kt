package slate.app.link

import android.util.Log
import slate.uuid.SlateUuids
import java.util.UUID

object LinkLog {
    const val TAG = "SlateLink"

    /**
     * In-app ring buffer mirroring what goes to logcat.
     *
     * Field debugging happens with the watch in one hand and the phone in the
     * other, usually nowhere near adb, so the log has to be readable and
     * shareable from the app itself.
     */
    private const val CAPACITY = 1000
    private val buffer = ArrayDeque<String>(CAPACITY)
    private val stamp = java.text.SimpleDateFormat("HH:mm:ss.SSS", java.util.Locale.US)

    @Synchronized
    private fun record(level: String, msg: String) {
        if (buffer.size >= CAPACITY) buffer.removeFirst()
        buffer.addLast("${stamp.format(java.util.Date())} $level $msg")
    }

    /** Newest last, ready to render or share. */
    @Synchronized
    fun snapshot(): List<String> = buffer.toList()

    @Synchronized
    fun clear() = buffer.clear()

    fun i(msg: String) {
        record("I", msg)
        Log.i(TAG, msg)
    }

    fun w(msg: String) {
        record("W", msg)
        Log.w(TAG, msg)
    }

    fun e(msg: String, t: Throwable? = null) {
        record("E", if (t != null) "$msg: ${t.message}" else msg)
        Log.e(TAG, msg, t)
    }
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
