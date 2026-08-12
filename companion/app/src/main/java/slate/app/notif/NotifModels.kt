package slate.app.notif

import slate.notif.NotifIconCategory
import slate.notif.NotifIconRef

/** Process-boundary-safe notification snapshot (no Android types). */
data class NotifAction(
    val id: String,
    val title: String,
    /** True if this is a remote-input / reply style action. */
    val isReply: Boolean = false,
)

data class NotifItem(
    val key: String,
    val packageName: String,
    /** Human-readable notifying app name for the watch stub list. */
    val appLabel: String,
    val title: String,
    val text: String,
    val whenMs: Long,
    val ongoing: Boolean,
    val clearable: Boolean,
    val importance: Int,
    val isGroupSummary: Boolean,
    val icon: NotifIconRef,
    val actions: List<NotifAction>,
) {
    val category: NotifIconCategory get() = icon.category

    /** Fields that affect the watch stub / vibe decision (ignore whenMs churn). */
    fun sameForWatch(other: NotifItem): Boolean =
        key == other.key &&
            packageName == other.packageName &&
            title == other.title &&
            text == other.text &&
            ongoing == other.ongoing &&
            clearable == other.clearable &&
            icon == other.icon
}

sealed class NotifChange {
    /**
     * @param silent true for NLS reconnect / shade restore — watch retains
     * without wake/haptic (FLAG_SILENT). Live posts stay loud.
     */
    data class Upserted(val item: NotifItem, val silent: Boolean = false) : NotifChange()
    data class Removed(val key: String) : NotifChange()
    data object Cleared : NotifChange()
}
