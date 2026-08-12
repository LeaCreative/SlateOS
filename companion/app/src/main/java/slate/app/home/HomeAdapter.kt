package slate.app.home

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.app.repo.RepoPrefs
import java.net.HttpURLConnection
import java.net.URI
import java.net.URL

/**
 * Home Assistant REST adapter. Talks to the HA *server*, not the HA phone app.
 * Credentials and entity list come from sub-app settings (store / RepoPrefs).
 */
class HomeAdapter(
    private val context: Context,
    private val appId: String,
    private val scope: CoroutineScope,
    private val onEvent: (json: String) -> Unit,
) {
    private var job: Job? = null
    private val prefs = RepoPrefs(context)

    fun refresh() {
        runOp("refresh") { base, token, entities ->
            emit(JSONObject().put("type", "status").put("state", "loading"))
            val arr = JSONArray()
            for (id in entities) {
                val state = getState(base, token, id) ?: continue
                arr.put(state)
            }
            emit(JSONObject().put("type", "states").put("entities", arr))
        }
    }

    fun toggle(entityId: String) {
        runOp("toggle") { base, token, entities ->
            val id = entityId.trim()
            if (id.isEmpty() || id !in entities) {
                emitStatus("error", "entity not allowed")
                return@runOp
            }
            val domain = id.substringBefore('.', "")
            if (domain.isEmpty()) {
                emitStatus("error", "bad entity")
                return@runOp
            }
            postService(base, token, domain, "toggle", JSONObject().put("entity_id", id))
            refreshStates(base, token, entities)
        }
    }

    fun set(entityId: String, brightness: Int?) {
        runOp("set") { base, token, entities ->
            val id = entityId.trim()
            if (id.isEmpty() || id !in entities) {
                emitStatus("error", "entity not allowed")
                return@runOp
            }
            val domain = id.substringBefore('.', "")
            val data = JSONObject().put("entity_id", id)
            if (brightness != null) {
                data.put("brightness", brightness.coerceIn(0, 255))
            }
            val service = when {
                domain == "light" && brightness != null && brightness <= 0 -> "turn_off"
                domain == "light" -> "turn_on"
                else -> "turn_on"
            }
            postService(base, token, domain, service, data)
            refreshStates(base, token, entities)
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private fun refreshStates(base: String, token: String, entities: List<String>) {
        val arr = JSONArray()
        for (id in entities) {
            val state = getState(base, token, id) ?: continue
            arr.put(state)
        }
        emit(JSONObject().put("type", "states").put("entities", arr))
    }

    private fun runOp(name: String, block: suspend (base: String, token: String, entities: List<String>) -> Unit) {
        job?.cancel()
        job = scope.launch {
            try {
                val base = (prefs.subAppSetting(appId, "baseUrl") ?: "").trim().trimEnd('/')
                val token = (prefs.subAppSetting(appId, "token") ?: "").trim()
                val entities = parseEntities(prefs.subAppSetting(appId, "entities") ?: "")
                if (base.isEmpty() || token.isEmpty()) {
                    emitStatus("error", "set baseUrl and token")
                    return@launch
                }
                if (!hostAllowed(base)) {
                    emitStatus("denied", "bad baseUrl")
                    return@launch
                }
                if (entities.isEmpty()) {
                    emitStatus("error", "set entities")
                    return@launch
                }
                withContext(Dispatchers.IO) { block(base, token, entities) }
            } catch (t: Throwable) {
                LinkLog.w("home.$name failed: ${t.message}")
                emitStatus("error", t.message ?: name)
            }
        }
    }

    private fun hostAllowed(baseUrl: String): Boolean {
        return try {
            val u = URI(baseUrl)
            val scheme = u.scheme?.lowercase()
            if (scheme != "http" && scheme != "https") return false
            !u.host.isNullOrBlank()
        } catch (_: Throwable) {
            false
        }
    }

    private fun parseEntities(raw: String): List<String> =
        raw.split(',', ' ', '\n', '\t')
            .map { it.trim() }
            .filter { it.contains('.') }
            .distinct()
            .take(MAX_ENTITIES)

    private fun getState(base: String, token: String, entityId: String): JSONObject? {
        val body = http("GET", "$base/api/states/$entityId", token, null) ?: return null
        val o = JSONObject(body)
        val attrs = o.optJSONObject("attributes")
        val out = JSONObject()
            .put("id", o.optString("entity_id", entityId))
            .put("name", attrs?.optString("friendly_name", entityId) ?: entityId)
            .put("state", o.optString("state", ""))
            .put("domain", entityId.substringBefore('.', ""))
        if (attrs != null && attrs.has("brightness")) {
            out.put("brightness", attrs.optInt("brightness"))
        }
        return out
    }

    private fun postService(
        base: String,
        token: String,
        domain: String,
        service: String,
        data: JSONObject,
    ) {
        http("POST", "$base/api/services/$domain/$service", token, data.toString())
    }

    private fun http(method: String, url: String, token: String, body: String?): String? {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = method
            connectTimeout = 8_000
            readTimeout = 8_000
            setRequestProperty("Authorization", "Bearer $token")
            setRequestProperty("Content-Type", "application/json")
            if (body != null) {
                doOutput = true
                outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            }
        }
        return try {
            val code = conn.responseCode
            val stream = if (code in 200..299) conn.inputStream else conn.errorStream
            val text = stream?.bufferedReader()?.use { it.readText() }.orEmpty()
            if (code !in 200..299) {
                throw IllegalStateException("HTTP $code ${text.take(60)}")
            }
            text.ifEmpty { "{}" }
        } finally {
            conn.disconnect()
        }
    }

    private fun emit(o: JSONObject) = onEvent(o.toString())

    private fun emitStatus(state: String, detail: String) {
        emit(
            JSONObject()
                .put("type", "status")
                .put("state", state)
                .put("detail", detail.take(80)),
        )
    }

    companion object {
        private const val MAX_ENTITIES = 8
    }
}
