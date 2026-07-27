package slate.script

/**
 * Resource governor (§6.5). Pure logic — no Android types.
 *
 * Kill on single overrun; disable after [disableAfterViolations] repeats.
 */
class Governor(
    private val disableAfterViolations: Int = 3,
) {
    data class Limits(
        val renderMs: Long = 50L,
        val eventMs: Long = 200L,
        val heapBytes: Long = 4L * 1024L * 1024L,
        val minTimerMs: Long = 1000L,
        val storageBytes: Int = 256 * 1024,
        val httpPerHour: Int = 60,
        val displayFgPerSec: Int = 10,
        val displayAmbientPerMin: Int = 1,
    )

    enum class Kind {
        RenderTimeout,
        EventTimeout,
        Heap,
        TimerTooFast,
        StorageQuota,
        HttpQuota,
        DisplayQuota,
        PermissionDenied,
    }

    data class Violation(
        val kind: Kind,
        val detail: String,
        val atMs: Long,
    )

    var limits: Limits = Limits()
    private val violations = ArrayList<Violation>()
    private var disabled = false
    private var disableReason: String? = null

    val isDisabled: Boolean get() = disabled
    val reason: String? get() = disableReason
    val violationLog: List<Violation> get() = violations.toList()

    fun reset() {
        violations.clear()
        disabled = false
        disableReason = null
    }

    fun noteViolation(kind: Kind, detail: String, nowMs: Long): Boolean {
        violations += Violation(kind, detail, nowMs)
        val recent = violations.count { it.kind == kind }
        if (recent >= disableAfterViolations) {
            disabled = true
            disableReason = "$kind x$recent: $detail"
            return true
        }
        return false
    }

    /** @return true if still allowed; false if killed/disabled */
    fun checkDuration(kind: Kind, elapsedMs: Long, nowMs: Long): Boolean {
        if (disabled) return false
        val limit = when (kind) {
            Kind.RenderTimeout -> limits.renderMs
            Kind.EventTimeout -> limits.eventMs
            else -> return true
        }
        if (elapsedMs > limit) {
            noteViolation(kind, "elapsed=${elapsedMs}ms limit=${limit}ms", nowMs)
            // Single render/event overrun kills for this call; disable after repeats.
            return false
        }
        return true
    }

    fun checkTimerInterval(intervalMs: Long, nowMs: Long): Boolean {
        if (disabled) return false
        if (intervalMs < limits.minTimerMs) {
            noteViolation(Kind.TimerTooFast, "interval=$intervalMs", nowMs)
            return false
        }
        return true
    }

    fun checkStorage(usedBytes: Int, adding: Int, nowMs: Long): Boolean {
        if (disabled) return false
        if (usedBytes + adding > limits.storageBytes) {
            noteViolation(Kind.StorageQuota, "used=$usedBytes adding=$adding", nowMs)
            return false
        }
        return true
    }

    fun denyPermission(perm: ScriptPermission, nowMs: Long) {
        noteViolation(Kind.PermissionDenied, perm.id, nowMs)
    }
}
