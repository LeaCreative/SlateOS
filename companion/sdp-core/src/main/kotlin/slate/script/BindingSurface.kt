package slate.script

import org.json.JSONObject
import slate.host.HostOutbound
import java.net.URI

/**
 * Permission-gated binding surface (§6.3).
 *
 * Whitelist only — no reflection, filesystem, or raw Android objects.
 * Effective permission = manifest ∩ hostHeld ∩ sourceCeiling.
 */
class BindingSurface(
    private val script: ScriptManifest,
    private val governor: Governor,
    private val hostHeld: Set<ScriptPermission>,
    private val sourceCeiling: Set<ScriptPermission>,
    private val store: MutableMap<String, String>,
    private val nowMs: () -> Long,
    private val onTimerSet: (id: String, intervalMs: Long) -> Unit,
    private val onTimerClear: (id: String) -> Unit,
    private val appId: String,
    /**
     * Fired when an adapter command is stripped for missing permission
     * (or http host not allowlisted). Host pushes a status event so the
     * script is not stuck on Loading.
     */
    private val onAdapterDenied: (adapter: String, command: String) -> Unit = { _, _ -> },
) {
    private var storageBytes: Int = store.values.sumOf { it.toByteArray().size + 8 }

    fun effective(p: ScriptPermission): Boolean =
        script.allows(p) && p in hostHeld && p in sourceCeiling

    fun injectStoreJson(): String {
        val o = JSONObject()
        for ((k, v) in store) o.put(k, v)
        return o.toString()
    }

    /** Apply adapter commands; drop ones handled here; deny unknowns requiring perms. */
    fun filterOutbound(messages: List<HostOutbound>): List<HostOutbound> {
        val kept = ArrayList<HostOutbound>(messages.size)
        for (m in messages) {
            when (m) {
                is HostOutbound.AdapterCommand -> {
                    when (m.adapter) {
                        "store" -> handleStore(m)
                        "timer" -> handleTimer(m)
                        "http" -> {
                            if (allowHttp(m)) kept += m
                        }
                        "notifications", "media", "location", "health", "haptic",
                        "nav", "camera", "phone", "map", "news", "weather",
                        -> {
                            val perm = when (m.adapter) {
                                "notifications" -> ScriptPermission.Notifications
                                "media" -> ScriptPermission.Media
                                "location" -> ScriptPermission.Location
                                "health" -> ScriptPermission.HealthRead
                                "nav" -> ScriptPermission.Navigation
                                "camera" -> ScriptPermission.Camera
                                "phone" -> ScriptPermission.Vibrate
                                "map" -> ScriptPermission.Location
                                "news" -> ScriptPermission.News
                                "weather" -> ScriptPermission.Weather
                                else -> null
                            }
                            if (perm != null && !effective(perm)) {
                                deny(perm)
                                onAdapterDenied(m.adapter, m.command)
                            } else {
                                kept += m
                            }
                        }
                        else -> kept += m
                    }
                }
                else -> kept += m
            }
        }
        return kept
    }

    private fun handleStore(cmd: HostOutbound.AdapterCommand) {
        if (!effective(ScriptPermission.Storage)) {
            deny(ScriptPermission.Storage)
            return
        }
        when (cmd.command) {
            "set" -> {
                val o = JSONObject(cmd.payloadJson)
                val key = o.getString("key")
                val value = o.getString("value")
                val adding = key.toByteArray().size + value.toByteArray().size + 8
                val prev = store[key]?.toByteArray()?.size ?: 0
                val used = storageBytes - prev
                val cap = minOf(script.storageQuotaBytes, governor.limits.storageBytes)
                if (used + adding > cap) {
                    governor.checkStorage(used, adding, nowMs())
                    ScriptConsole.quota(appId, "storage deny used=$used adding=$adding cap=$cap")
                    return
                }
                store[key] = value
                storageBytes = used + adding
                ScriptConsole.quota(appId, "storage used=$storageBytes/$cap")
            }
            "clear" -> {
                store.clear()
                storageBytes = 0
            }
        }
    }

    private fun handleTimer(cmd: HostOutbound.AdapterCommand) {
        when (cmd.command) {
            "set" -> {
                val o = JSONObject(cmd.payloadJson)
                val id = o.getString("id")
                val ms = o.getLong("intervalMs")
                if (!governor.checkTimerInterval(ms, nowMs())) {
                    ScriptConsole.violation(appId, "timer too fast $ms")
                    return
                }
                onTimerSet(id, ms)
            }
            "clear" -> {
                val o = JSONObject(cmd.payloadJson)
                onTimerClear(o.getString("id"))
            }
        }
    }

    /** Permission + allowlist; keep command for the host HTTP adapter. */
    private fun allowHttp(cmd: HostOutbound.AdapterCommand): Boolean {
        if (!effective(ScriptPermission.Http)) {
            deny(ScriptPermission.Http)
            onAdapterDenied("http", cmd.command)
            return false
        }
        val o = try {
            JSONObject(cmd.payloadJson)
        } catch (_: Throwable) {
            ScriptConsole.violation(appId, "http bad payload")
            onAdapterDenied("http", cmd.command)
            return false
        }
        val url = o.optString("url", "")
        val host = hostOf(url).ifEmpty { o.optString("host", "") }
        if (host.isEmpty() || host !in script.allowedHosts) {
            governor.noteViolation(Governor.Kind.HttpQuota, "host not allowed: $host", nowMs())
            ScriptConsole.violation(appId, "http host denied: $host")
            onAdapterDenied("http", cmd.command)
            return false
        }
        return true
    }

    private fun deny(p: ScriptPermission) {
        governor.denyPermission(p, nowMs())
        ScriptConsole.violation(appId, "permission denied: ${p.id}")
    }

    companion object {
        fun hostOf(rawUrl: String): String = try {
            URI(rawUrl.trim()).host?.lowercase().orEmpty()
        } catch (_: Throwable) {
            ""
        }
    }
}
