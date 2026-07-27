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
}

sealed class NotifChange {
    data class Upserted(val item: NotifItem) : NotifChange()
    data class Removed(val key: String) : NotifChange()
    data object Cleared : NotifChange()
}
