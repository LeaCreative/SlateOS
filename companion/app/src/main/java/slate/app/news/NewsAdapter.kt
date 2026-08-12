package slate.app.news

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.news.ArticlePager
import slate.news.NewsItem
import slate.news.RssParser
import java.net.HttpURLConnection
import java.net.URL

/**
 * Host-side RSS/Atom fetch for the official news sub-app.
 *
 * Network and XML stay out of the JS isolate (map / Overpass precedent). The
 * script draws list and article screens from [onEvent] payloads.
 */
class NewsAdapter(
    private val scope: CoroutineScope,
    private val onEvent: (json: String) -> Unit,
    private val httpGet: suspend (url: String) -> String = { defaultGet(it) },
) {
    private var job: Job? = null
    private var items: List<NewsItem> = emptyList()
    private val pageCache = mutableMapOf<String, List<String>>()

    fun list(feedUrl: String) {
        job?.cancel()
        items = emptyList()
        pageCache.clear()
        val url = feedUrl.trim()
        if (url.isEmpty()) {
            emit(JSONObject().put("type", "status").put("state", "need_url"))
            return
        }
        if (!isHttpUrl(url)) {
            emit(
                JSONObject()
                    .put("type", "status")
                    .put("state", "error")
                    .put("detail", "invalid url"),
            )
            return
        }
        emit(JSONObject().put("type", "status").put("state", "loading"))
        LinkLog.i("news.list fetch ${url.take(80)}")
        job = scope.launch {
            try {
                val xml = httpGet(url)
                val parsed = RssParser.parse(xml, LIMIT)
                items = parsed
                if (parsed.isEmpty()) {
                    emit(JSONObject().put("type", "status").put("state", "empty"))
                    return@launch
                }
                val arr = JSONArray()
                for (it in parsed) {
                    arr.put(
                        JSONObject()
                            .put("id", it.id)
                            .put("title", it.title),
                    )
                }
                emit(JSONObject().put("type", "list").put("items", arr))
            } catch (t: Throwable) {
                LinkLog.w("news.list failed: ${t.message}")
                emit(
                    JSONObject()
                        .put("type", "status")
                        .put("state", "error")
                        .put("detail", (t.message ?: "network").take(80)),
                )
            }
        }
    }

    /**
     * Returns the page JSON for the compositor to deliver synchronously after
     * input handling. Async [onEvent] is only used for network list fetch.
     */
    fun page(id: String, page: Int): String {
        val item = items.firstOrNull { it.id == id }
        if (item == null) {
            return JSONObject()
                .put("type", "status")
                .put("state", "error")
                .put("detail", "unknown article")
                .toString()
        }
        val pages = pageCache.getOrPut(id) { ArticlePager.pages(item.body) }
        val idx = page.coerceIn(0, pages.lastIndex)
        return JSONObject()
            .put("type", "page")
            .put("id", id)
            .put("page", idx)
            .put("pageCount", pages.size)
            .put("text", pages[idx])
            .toString()
    }

    fun stop() {
        job?.cancel()
        job = null
        items = emptyList()
        pageCache.clear()
    }

    private fun emit(o: JSONObject) {
        onEvent(o.toString())
    }

    companion object {
        const val LIMIT = 8

        fun isHttpUrl(raw: String): Boolean {
            return try {
                val u = URL(raw)
                u.protocol == "http" || u.protocol == "https"
            } catch (_: Throwable) {
                false
            }
        }

        suspend fun defaultGet(url: String): String = withContext(Dispatchers.IO) {
            val conn = (URL(url).openConnection() as HttpURLConnection).apply {
                connectTimeout = 15_000
                readTimeout = 20_000
                instanceFollowRedirects = true
                requestMethod = "GET"
                setRequestProperty("User-Agent", "SlateNews/1.0 (PineTime companion)")
                setRequestProperty("Accept", "application/rss+xml, application/atom+xml, application/xml, text/xml, */*")
            }
            try {
                val code = conn.responseCode
                val stream = if (code in 200..299) conn.inputStream else conn.errorStream
                    ?: error("HTTP $code")
                if (code !in 200..299) error("HTTP $code")
                // Cap body so a multi-MB feed cannot hang the isolate/event path.
                val limit = MAX_FEED_BYTES
                val buf = ByteArray(8_192)
                val out = java.io.ByteArrayOutputStream()
                var total = 0
                stream.use { input ->
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        total += n
                        if (total > limit) error("feed too large (>${limit / 1024} KiB)")
                        out.write(buf, 0, n)
                    }
                }
                out.toString(Charsets.UTF_8.name())
            } finally {
                conn.disconnect()
            }
        }

        private const val MAX_FEED_BYTES = 512 * 1024
    }
}
