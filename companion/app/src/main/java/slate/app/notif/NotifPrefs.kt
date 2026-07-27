package slate.app.notif

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/** User prefs: which packages may raise INTERRUPT focus. */
class NotifPrefs(context: Context) {
    private val sp = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** When true, only packages in [interruptAllowlist] may interrupt. */
    var interruptFilterEnabled: Boolean
        get() = sp.getBoolean(KEY_FILTER, false)
        set(v) = sp.edit().putBoolean(KEY_FILTER, v).apply()

    var interruptAllowlist: Set<String>
        get() = sp.getStringSet(KEY_ALLOW, emptySet()) ?: emptySet()
        set(v) = sp.edit().putStringSet(KEY_ALLOW, v).apply()

    fun mayInterrupt(packageName: String, importance: Int): Boolean {
        // Android importance: 0 NONE … 5 MAX. Treat HIGH(4)+ as interrupt candidates.
        if (importance < 4) return false
        if (!interruptFilterEnabled) return true
        return packageName in interruptAllowlist
    }

    fun allowInterrupt(packageName: String) {
        interruptAllowlist = interruptAllowlist + packageName
    }

    fun denyInterrupt(packageName: String) {
        interruptAllowlist = interruptAllowlist - packageName
    }

    companion object {
        private const val PREFS = "slate_notif"
        private const val KEY_FILTER = "interrupt_filter"
        private const val KEY_ALLOW = "interrupt_allow"
    }
}

fun List<NotifItem>.toJsonArray(): JSONArray {
    val arr = JSONArray()
    for (n in this) {
        arr.put(
            JSONObject()
                .put("key", n.key)
                .put("packageName", n.packageName)
                .put("title", n.title)
                .put("text", n.text)
                .put("whenMs", n.whenMs)
                .put("ongoing", n.ongoing)
                .put("clearable", n.clearable)
                .put("importance", n.importance)
                .put("category", n.icon.category.name)
                .put("monogram", n.icon.monogram.toString())
                .put(
                    "actions",
                    JSONArray().also { a ->
                        n.actions.forEach { act ->
                            a.put(
                                JSONObject()
                                    .put("id", act.id)
                                    .put("title", act.title)
                                    .put("isReply", act.isReply),
                            )
                        }
                    },
                ),
        )
    }
    return arr
}
