package slate.script

import slate.host.HostInbound
import slate.host.HostOutbound
import slate.host.PriorityClass
import java.util.Base64
import org.json.JSONArray
import org.json.JSONObject

/**
 * JSON bridge for process-boundary JS isolates (M12).
 * Display lists travel as base64 of raw SDP bytes.
 */
object HostJson {
    fun encodeInbound(msg: HostInbound): String {
        val o = JSONObject()
        when (msg) {
            is HostInbound.Create -> {
                o.put("type", "create")
                o.put("watchProtocolVersion", msg.watchProtocolVersion)
            }
            HostInbound.Start -> o.put("type", "start")
            HostInbound.Focus -> o.put("type", "focus")
            HostInbound.Blur -> o.put("type", "blur")
            HostInbound.Stop -> o.put("type", "stop")
            HostInbound.Destroy -> o.put("type", "destroy")
            HostInbound.Render -> o.put("type", "render")
            is HostInbound.Input -> {
                o.put("type", "input")
                o.put("op", msg.op)
                o.put("elemId", msg.elemId)
                o.put("x", msg.x)
                o.put("y", msg.y)
                o.put("dir", msg.dir)
                o.put("action", msg.action)
                o.put("count", msg.count)
                o.put("edge", msg.edge)
                o.put("distance", msg.distance)
                o.put("durationMs", msg.durationMs)
                o.put("reason", msg.reason)
            }
            is HostInbound.SystemEvent -> {
                o.put("type", "event")
                o.put("source", msg.source)
                o.put("data", msg.jsonPayload)
            }
        }
        return o.toString()
    }

    fun decodeOutboundList(json: String): List<HostOutbound> {
        val arr = try {
            JSONArray(json)
        } catch (_: Throwable) {
            return emptyList()
        }
        val out = ArrayList<HostOutbound>(arr.length())
        for (i in 0 until arr.length()) {
            val o = arr.optJSONObject(i) ?: continue
            when (o.optString("type")) {
                "pushDisplayList" -> {
                    val b64 = o.optString("displayListBase64")
                    if (b64.isNotEmpty()) {
                        out += HostOutbound.PushDisplayList(Base64.getDecoder().decode(b64))
                    }
                }
                "invalidate" -> out += HostOutbound.Invalidate
                "requestFocus" -> {
                    val p = PriorityClass.fromManifest(o.optString("priority", "normal"))
                    out += HostOutbound.RequestFocus(p)
                }
                "relinquishFocus" -> out += HostOutbound.RelinquishFocus
                "inputHandled" -> out += HostOutbound.InputHandled
                "inputUnhandled" -> out += HostOutbound.InputUnhandled
                "log" -> out += HostOutbound.Log(
                    o.optString("level", "info"),
                    o.optString("message", ""),
                )
                "adapter" -> out += HostOutbound.AdapterCommand(
                    adapter = o.optString("adapter"),
                    command = o.optString("command"),
                    payloadJson = o.optString("payload", "{}"),
                )
            }
        }
        return out
    }
}
