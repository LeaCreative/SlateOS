package slate.script

import java.util.ArrayDeque

/**
 * In-app developer console ring (§6.5). Thread-safe; UI observes [snapshot].
 */
object ScriptConsole {
    data class Entry(
        val atMs: Long,
        val appId: String,
        val kind: String,
        val message: String,
        val ms: Long? = null,
    )

    private const val CAPACITY = 500
    private val lock = Any()
    private val ring = ArrayDeque<Entry>(CAPACITY)
    private val listeners = ArrayList<() -> Unit>()

    fun clear() = synchronized(lock) {
        ring.clear()
        notifyListeners()
    }

    fun log(appId: String, level: String, message: String, nowMs: Long = System.currentTimeMillis()) {
        add(Entry(nowMs, appId, "log:$level", message))
    }

    fun timing(appId: String, label: String, ms: Long, nowMs: Long = System.currentTimeMillis()) {
        add(Entry(nowMs, appId, "timing", label, ms))
    }

    fun violation(appId: String, detail: String, nowMs: Long = System.currentTimeMillis()) {
        add(Entry(nowMs, appId, "violation", detail))
    }

    fun quota(appId: String, detail: String, nowMs: Long = System.currentTimeMillis()) {
        add(Entry(nowMs, appId, "quota", detail))
    }

    fun ipc(appId: String, detail: String, ms: Long, nowMs: Long = System.currentTimeMillis()) {
        add(Entry(nowMs, appId, "ipc", detail, ms))
    }

    fun snapshot(): List<Entry> = synchronized(lock) { ring.toList() }

    fun addListener(l: () -> Unit) = synchronized(lock) { listeners += l }

    fun removeListener(l: () -> Unit) = synchronized(lock) { listeners.remove(l) }

    private fun add(e: Entry) = synchronized(lock) {
        if (ring.size >= CAPACITY) ring.removeFirst()
        ring.addLast(e)
        notifyListeners()
    }

    private fun notifyListeners() {
        for (l in listeners.toList()) {
            try {
                l()
            } catch (_: Throwable) {
            }
        }
    }
}
