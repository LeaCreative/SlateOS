package slate.app.notif

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.ConcurrentHashMap

/** In-memory notification mirror shared by the listener and compositor host. */
object NotifStore {
    private const val MAX_ITEMS = 32

    private val items = ConcurrentHashMap<String, NotifItem>()
    private val _snapshot = MutableStateFlow<List<NotifItem>>(emptyList())
    val snapshot: StateFlow<List<NotifItem>> = _snapshot.asStateFlow()

    private val _changes = MutableSharedFlow<NotifChange>(extraBufferCapacity = 64)
    val changes: SharedFlow<NotifChange> = _changes.asSharedFlow()

    /** PendingIntent keys: notifKey → (actionId → runnable). Filled by listener. */
    private val actionHandlers = ConcurrentHashMap<String, ConcurrentHashMap<String, () -> Unit>>()

    fun upsert(item: NotifItem) {
        items[item.key] = item
        trim()
        publish()
        _changes.tryEmit(NotifChange.Upserted(item))
    }

    fun remove(key: String) {
        items.remove(key)
        actionHandlers.remove(key)
        publish()
        _changes.tryEmit(NotifChange.Removed(key))
    }

    fun clear() {
        items.clear()
        actionHandlers.clear()
        publish()
        _changes.tryEmit(NotifChange.Cleared)
    }

    fun get(key: String): NotifItem? = items[key]

    fun registerActions(key: String, handlers: Map<String, () -> Unit>) {
        actionHandlers[key] = ConcurrentHashMap(handlers)
    }

    fun invokeAction(key: String, actionId: String): Boolean {
        val fn = actionHandlers[key]?.get(actionId) ?: return false
        return try {
            fn()
            true
        } catch (_: Throwable) {
            false
        }
    }

    fun dismissLocal(key: String) {
        remove(key)
    }

    private fun trim() {
        if (items.size <= MAX_ITEMS) return
        val ordered = items.values.sortedBy { it.whenMs }
        val drop = ordered.take(items.size - MAX_ITEMS)
        drop.forEach { items.remove(it.key) }
    }

    private fun publish() {
        _snapshot.value = items.values.sortedByDescending { it.whenMs }
    }
}
