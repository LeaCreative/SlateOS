package slate.compositor

import slate.host.PriorityClass

/**
 * Per-app push quotas (roadmap §6.5 / M8):
 * - Foreground (focused, non-ambient): 10 / second
 * - Ambient: 1 / minute
 * - Non-focused non-ambient: no pushes
 */
class PushQuota(
    private val nowMs: () -> Long,
) {
    private data class Bucket(var windowStartMs: Long, var count: Int)

    private val buckets = HashMap<String, Bucket>()

    fun tryConsume(appId: String, priority: PriorityClass, isFocused: Boolean): Boolean {
        val limit: Int
        val windowMs: Long
        when {
            priority == PriorityClass.AMBIENT -> {
                limit = 1
                windowMs = 60_000L
            }
            isFocused -> {
                limit = 10
                windowMs = 1_000L
            }
            else -> return false
        }
        val t = nowMs()
        val b = buckets.getOrPut(appId) { Bucket(t, 0) }
        if (t - b.windowStartMs >= windowMs) {
            b.windowStartMs = t
            b.count = 0
        }
        if (b.count >= limit) return false
        b.count++
        return true
    }

    fun reset(appId: String) {
        buckets.remove(appId)
    }
}
