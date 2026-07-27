package slate.app.repo

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL

/** Minimal HTTPS fetcher (no third-party HTTP stack). */
object RepoHttp {
    suspend fun getBytes(url: String, maxBytes: Int = 8 * 1024 * 1024): ByteArray =
        withContext(Dispatchers.IO) {
            val conn = (URL(url).openConnection() as HttpURLConnection).apply {
                connectTimeout = 15_000
                readTimeout = 30_000
                instanceFollowRedirects = true
                requestMethod = "GET"
                setRequestProperty("Accept", "*/*")
            }
            try {
                val code = conn.responseCode
                if (code !in 200..299) {
                    throw RepoHttpException("HTTP $code for $url")
                }
                conn.inputStream.use { input ->
                    val out = ByteArrayOutputStream()
                    val buf = ByteArray(8192)
                    var total = 0
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        total += n
                        if (total > maxBytes) throw RepoHttpException("response too large")
                        out.write(buf, 0, n)
                    }
                    out.toByteArray()
                }
            } finally {
                conn.disconnect()
            }
        }

    suspend fun getText(url: String): String =
        getBytes(url, maxBytes = 2 * 1024 * 1024).toString(Charsets.UTF_8)
}

class RepoHttpException(message: String) : Exception(message)
