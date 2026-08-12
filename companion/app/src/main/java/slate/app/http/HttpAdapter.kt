package slate.app.http

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.script.BindingSurface
import java.net.HttpURLConnection
import java.net.URL

/**
 * Host-side HTTP for JS sub-apps. Bodies never enter as streams the isolate
 * can abuse — capped UTF-8 text only; host allowlist already enforced in
 * [BindingSurface].
 */
class HttpAdapter(
    private val scope: CoroutineScope,
    private val onEvent: (json: String) -> Unit,
) {
    private val jobs = mutableMapOf<String, Job>()

    fun get(id: String, url: String) = request(id, "GET", url, body = null)

    fun post(id: String, url: String, body: String?) = request(id, "POST", url, body)

    fun cancel(id: String) {
        jobs.remove(id)?.cancel()
    }

    fun stop() {
        jobs.values.forEach { it.cancel() }
        jobs.clear()
    }

    private fun request(id: String, method: String, url: String, body: String?) {
        val reqId = id.ifBlank { "r${System.currentTimeMillis()}" }
        jobs.remove(reqId)?.cancel()
        if (!isHttpUrl(url)) {
            emitError(reqId, "invalid url")
            return
        }
        if (body != null && body.toByteArray(Charsets.UTF_8).size > MAX_REQUEST_BYTES) {
            emitError(reqId, "body too large")
            return
        }
        jobs[reqId] = scope.launch {
            try {
                val result = withContext(Dispatchers.IO) { execute(method, url, body) }
                onEvent(
                    JSONObject()
                        .put("type", "response")
                        .put("id", reqId)
                        .put("status", result.status)
                        .put("body", result.body)
                        .toString(),
                )
            } catch (t: Throwable) {
                LinkLog.w("http.$method failed: ${t.message}")
                emitError(reqId, (t.message ?: "network").take(80))
            } finally {
                jobs.remove(reqId)
            }
        }
    }

    private fun emitError(id: String, detail: String) {
        onEvent(
            JSONObject()
                .put("type", "error")
                .put("id", id)
                .put("detail", detail)
                .toString(),
        )
    }

    private data class Result(val status: Int, val body: String)

    companion object {
        const val MAX_RESPONSE_BYTES = 64 * 1024
        const val MAX_REQUEST_BYTES = 16 * 1024

        fun isHttpUrl(raw: String): Boolean = try {
            val u = URL(raw)
            u.protocol == "http" || u.protocol == "https"
        } catch (_: Throwable) {
            false
        }

        fun hostOf(url: String): String = BindingSurface.hostOf(url)

        private fun execute(method: String, url: String, body: String?): Result {
            val conn = (URL(url).openConnection() as HttpURLConnection).apply {
                connectTimeout = 15_000
                readTimeout = 20_000
                instanceFollowRedirects = true
                requestMethod = method
                setRequestProperty("User-Agent", "SlateHttp/1.0 (PineTime companion)")
                setRequestProperty("Accept", "application/json, text/plain, text/*, */*")
                if (body != null) {
                    doOutput = true
                    setRequestProperty("Content-Type", "application/json; charset=utf-8")
                }
            }
            try {
                if (body != null) {
                    conn.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
                }
                val code = conn.responseCode
                val stream = if (code in 200..299) conn.inputStream else conn.errorStream
                    ?: error("HTTP $code")
                val text = readCapped(stream)
                if (code !in 200..299) error("HTTP $code")
                return Result(code, text)
            } finally {
                conn.disconnect()
            }
        }

        private fun readCapped(stream: java.io.InputStream): String {
            val buf = ByteArray(8_192)
            val out = java.io.ByteArrayOutputStream()
            var total = 0
            stream.use { input ->
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    total += n
                    if (total > MAX_RESPONSE_BYTES) {
                        error("response too large (>${MAX_RESPONSE_BYTES / 1024} KiB)")
                    }
                    out.write(buf, 0, n)
                }
            }
            return out.toString(Charsets.UTF_8.name())
        }
    }
}
