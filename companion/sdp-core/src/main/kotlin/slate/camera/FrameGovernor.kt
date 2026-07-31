package slate.camera

/**
 * Frame-rate governor for camera → watch streaming.
 * Drops frames rather than queueing when throughput cannot keep up.
 */
class FrameGovernor(
    /** Sustained budget (bytes/sec) toward the watch. Default ~20 kB/s conservative. */
    var budgetBytesPerSec: Int = 20_000,
    /** Hard max in-flight frames (0 = only one outstanding). */
    var maxInFlight: Int = 1,
) {
    private var windowStartMs: Long = 0L
    private var bytesInWindow: Int = 0
    private var inFlight: Int = 0
    var accepted: Long = 0L
        private set
    var dropped: Long = 0L
        private set

    /** Returns true if [frameBytes] may be pushed now. */
    fun tryAccept(frameBytes: Int, nowMs: Long): Boolean {
        if (windowStartMs == 0L || nowMs - windowStartMs >= 1000L) {
            windowStartMs = nowMs
            bytesInWindow = 0
        }
        if (inFlight >= maxInFlight) {
            dropped++
            return false
        }
        if (bytesInWindow + frameBytes > budgetBytesPerSec) {
            dropped++
            return false
        }
        bytesInWindow += frameBytes
        inFlight++
        accepted++
        return true
    }

    fun onFrameAcked() {
        if (inFlight > 0) inFlight--
    }

    fun reset() {
        windowStartMs = 0L
        bytesInWindow = 0
        inFlight = 0
        accepted = 0L
        dropped = 0L
    }

    fun measuredFpsHint(frameBytes: Int): Double {
        if (frameBytes <= 0) return 0.0
        return budgetBytesPerSec.toDouble() / frameBytes.toDouble()
    }
}
