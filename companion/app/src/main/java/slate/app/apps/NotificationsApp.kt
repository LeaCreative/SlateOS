package slate.app.apps

import org.json.JSONArray
import org.json.JSONObject
import slate.dsl.displayList
import slate.generated.SdpWire
import slate.host.AppManifest
import slate.host.HostInbound
import slate.host.HostOutbound
import slate.host.KotlinSlateApp
import slate.host.PriorityClass
import slate.host.RefreshPolicy
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb

/**
 * Notifications sub-app (M9) — M8 contract, scrollable list + detail + actions.
 */
class NotificationsApp : KotlinSlateApp() {
    override val manifest = AppManifest(
        id = ID,
        name = "Notifications",
        version = "1.0.0",
        minProtocolVersion = 1,
        defaultPriority = PriorityClass.NORMAL,
        refresh = RefreshPolicy.OnChange,
    )

    private var items: List<Row> = emptyList()
    private var detailKey: String? = null

    data class Row(
        val key: String,
        val title: String,
        val text: String,
        val monogram: Char,
        val actions: List<Pair<String, String>>, // id to title
    )

    override fun onFocus(out: MutableList<HostOutbound>) {
        out.push(buildScreen())
    }

    override fun onRender(out: MutableList<HostOutbound>) {
        out.push(buildScreen())
    }

    override fun onSystemEvent(msg: HostInbound.SystemEvent, out: MutableList<HostOutbound>) {
        if (msg.source != SOURCE) return
        parseSnapshot(msg.jsonPayload)
        if (isFocused) {
            out.push(buildScreen())
        } else {
            out.invalidate()
            // High-priority interrupt requests are raised by CompositorHost; app may
            // also ask for NORMAL focus when user opens notifications later.
        }
    }

    override fun onInput(msg: HostInbound.Input, out: MutableList<HostOutbound>): Boolean {
        when (msg.op) {
            SdpWire.InputOp.BACK -> {
                if (detailKey != null) {
                    detailKey = null
                    out.push(buildScreen())
                    return true
                }
                out += HostOutbound.RelinquishFocus
                return true
            }
            SdpWire.InputOp.TAP -> {
                val id = msg.elemId
                if (detailKey == null) {
                    // List rows: element ids 100..100+n
                    val idx = id - ID_ROW0
                    if (idx in items.indices) {
                        detailKey = items[idx].key
                        out.push(buildScreen())
                        return true
                    }
                } else {
                    when (id) {
                        ID_DISMISS -> {
                            emitAction(out, detailKey!!, "dismiss")
                            detailKey = null
                            out.push(buildScreen())
                            return true
                        }
                        ID_SNOOZE -> {
                            emitAction(out, detailKey!!, "snooze")
                            detailKey = null
                            out.push(buildScreen())
                            return true
                        }
                        in ID_ACTION0 until ID_ACTION0 + 8 -> {
                            val row = items.find { it.key == detailKey } ?: return true
                            val ai = id - ID_ACTION0
                            val act = row.actions.getOrNull(ai) ?: return true
                            emitAction(out, row.key, act.first)
                            return true
                        }
                    }
                }
            }
        }
        return false
    }

    private fun emitAction(out: MutableList<HostOutbound>, key: String, actionId: String) {
        out += HostOutbound.AdapterCommand(
            adapter = "notifications",
            command = "action",
            payloadJson = JSONObject().put("key", key).put("actionId", actionId).toString(),
        )
        if (actionId == "dismiss" || actionId == "snooze") {
            items = items.filterNot { it.key == key }
        }
    }

    private fun parseSnapshot(json: String) {
        try {
            val root = JSONObject(json)
            val arr = root.optJSONArray("items") ?: JSONArray()
            val next = ArrayList<Row>()
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val acts = ArrayList<Pair<String, String>>()
                val aa = o.optJSONArray("actions") ?: JSONArray()
                for (j in 0 until aa.length()) {
                    val a = aa.getJSONObject(j)
                    acts += a.getString("id") to a.getString("title")
                }
                next += Row(
                    key = o.getString("key"),
                    title = o.optString("title"),
                    text = o.optString("text"),
                    monogram = o.optString("monogram", "?").firstOrNull() ?: '?',
                    actions = acts,
                )
            }
            items = next
            if (detailKey != null && items.none { it.key == detailKey }) {
                detailKey = null
            }
        } catch (_: Throwable) {
            // keep previous
        }
    }

    private fun buildScreen(): ByteArray {
        val detail = detailKey?.let { k -> items.find { it.key == k } }
        return if (detail != null) buildDetail(detail) else buildList()
    }

    private fun buildList(): ByteArray = displayList {
        palette(0, Colors.BLACK)
        palette(1, Colors.WHITE)
        palette(2, rgb(0x4208))
        clear(pal(0))
        text(Font.LARGE, 120, 12, Align.CENTER, pal(1), "Notifs")
        val rowH = 44
        val visible = items.take(12)
        val contentH = (visible.size * rowH).coerceAtLeast(1)
        scrollRegion(y = 36, h = 200, contentH = contentH) {
            visible.forEachIndexed { i, row ->
                val y = 36 + i * rowH
                element(id = ID_ROW0 + i, x = 4, y = y, w = 232, h = rowH - 4) {
                    rectRound(4, y, 232, rowH - 4, 6, pal(2), Style.FILL)
                    text(Font.LARGE, 16, y + 6, Align.LEFT, pal(1), row.monogram.toString())
                    text(Font.LARGE, 40, y + 6, Align.LEFT, pal(1), row.title.take(14))
                    text(Font.LARGE, 40, y + 22, Align.LEFT, pal(1), row.text.take(16))
                }
            }
        }
        commit()
    }

    private fun buildDetail(row: Row): ByteArray = displayList {
        palette(0, Colors.BLACK)
        palette(1, Colors.WHITE)
        palette(2, rgb(0x2104))
        palette(3, rgb(0x07E0))
        clear(pal(0))
        text(Font.LARGE, 16, 16, Align.LEFT, pal(1), row.monogram.toString())
        text(Font.LARGE, 40, 16, Align.LEFT, pal(1), row.title.take(16))
        text(Font.LARGE, 16, 48, Align.LEFT, pal(1), row.text.take(40))

        var x = 8
        val y = 160
        element(id = ID_DISMISS, x = x, y = y, w = 70, h = 36) {
            rectRound(x, y, 70, 36, 6, pal(2), Style.FILL)
            text(Font.LARGE, x + 35, y + 10, Align.CENTER, pal(1), "Del")
        }
        x += 78
        element(id = ID_SNOOZE, x = x, y = y, w = 70, h = 36) {
            rectRound(x, y, 70, 36, 6, pal(2), Style.FILL)
            text(Font.LARGE, x + 35, y + 10, Align.CENTER, pal(1), "Zzz")
        }
        row.actions.filter { it.first != "dismiss" && it.first != "snooze" }
            .take(2)
            .forEachIndexed { i, act ->
                val ax = 8 + i * 116
                val ay = 200
                element(id = ID_ACTION0 + i, x = ax, y = ay, w = 108, h = 32) {
                    rectRound(ax, ay, 108, 32, 6, pal(3), Style.FILL)
                    text(Font.LARGE, ax + 54, ay + 8, Align.CENTER, pal(0), act.second.take(8))
                }
            }
        commit()
    }

    companion object {
        const val ID = "slate.ref.notifications"
        const val SOURCE = "notifications"
        private const val ID_ROW0 = 100
        private const val ID_DISMISS = 10
        private const val ID_SNOOZE = 11
        private const val ID_ACTION0 = 20
    }
}
