package slate.host

/**
 * Screen priority classes for compositor arbitration (roadmap M8).
 *
 * Ordering: [CRITICAL] > [INTERRUPT] > [NORMAL] > [AMBIENT].
 */
enum class PriorityClass(val rank: Int) {
    AMBIENT(0),
    NORMAL(1),
    /** Calls, alarms, high-priority notifications. */
    INTERRUPT(2),
    CRITICAL(3),
    ;

    companion object {
        fun fromManifest(value: String): PriorityClass =
            entries.firstOrNull { it.name.equals(value, ignoreCase = true) }
                ?: NORMAL
    }
}

/**
 * How often an app may produce a new display list. The compositor still
 * coalesces and credit/quota-limits every push.
 */
sealed class RefreshPolicy {
    /** Push only when the app emits [HostOutbound.Invalidate] or [HostOutbound.PushDisplayList]. */
    data object OnChange : RefreshPolicy()

    /** Compositor requests a render at most every [intervalMs]. */
    data class Periodic(val intervalMs: Long) : RefreshPolicy() {
        init {
            require(intervalMs >= 1000L) { "periodic refresh minimum 1000 ms" }
        }
    }

    /** App must explicitly [HostOutbound.PushDisplayList]; Invalidate is ignored. */
    data object Manual : RefreshPolicy()
}

/**
 * Serializable app identity — mirrors `manifest.json` fields the compositor needs.
 * No Android types. Safe to JSON-encode across a process boundary.
 */
data class AppManifest(
    val id: String,
    val name: String,
    val version: String = "0.0.0",
    val minProtocolVersion: Int = 1,
    /** Semver string for host/script API — compositor does not enforce yet. */
    val minHostVersion: String = "0.1",
    val defaultPriority: PriorityClass = PriorityClass.NORMAL,
    val refresh: RefreshPolicy = RefreshPolicy.OnChange,
)
