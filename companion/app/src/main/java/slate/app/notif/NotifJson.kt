package slate.app.notif

import org.json.JSONArray
import org.json.JSONObject

fun List<NotifItem>.toJsonArray(): JSONArray {
    val arr = JSONArray()
    for (n in this) {
        arr.put(
            JSONObject()
                .put("key", n.key)
                .put("packageName", n.packageName)
                .put("appLabel", n.appLabel)
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
