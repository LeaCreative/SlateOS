package slate.news

/**
 * One feed entry after host-side parse. [body] is already HTML-stripped plain text.
 */
data class NewsItem(
    val id: String,
    val title: String,
    val body: String,
)

/**
 * Minimal RSS 2.0 / Atom parser. Enough for official demos — not a full feed toolkit.
 */
object RssParser {
    private val ITEM = Regex("(?is)<item\\b[^>]*>(.*?)</item>")
    private val ENTRY = Regex("(?is)<entry\\b[^>]*>(.*?)</entry>")
    private val TITLE = Regex("(?is)<title\\b[^>]*>(.*?)</title>")
    private val GUID = Regex("(?is)<guid\\b[^>]*>(.*?)</guid>")
    private val LINK_RSS = Regex("(?is)<link\\b[^>]*>(.*?)</link>")
    private val LINK_ATOM = Regex("(?is)<link\\b[^>]*href\\s*=\\s*\"([^\"]+)\"[^>]*/?>")
    private val DESC = Regex("(?is)<description\\b[^>]*>(.*?)</description>")
    private val CONTENT = Regex("(?is)<content:encoded\\b[^>]*>(.*?)</content:encoded>")
    private val SUMMARY = Regex("(?is)<summary\\b[^>]*>(.*?)</summary>")
    private val CONTENT_ATOM = Regex("(?is)<content\\b[^>]*>(.*?)</content>")
    private val ID_ATOM = Regex("(?is)<id\\b[^>]*>(.*?)</id>")

    fun parse(xml: String, limit: Int = 8): List<NewsItem> {
        val items = mutableListOf<NewsItem>()
        for (m in ITEM.findAll(xml)) {
            if (items.size >= limit) break
            parseBlock(m.groupValues[1], atom = false)?.let { items += it }
        }
        if (items.isEmpty()) {
            for (m in ENTRY.findAll(xml)) {
                if (items.size >= limit) break
                parseBlock(m.groupValues[1], atom = true)?.let { items += it }
            }
        }
        return items
    }

    private fun parseBlock(block: String, atom: Boolean): NewsItem? {
        val title = textOf(TITLE.find(block)?.groupValues?.get(1)).ifBlank { return null }
        val bodyRaw = if (atom) {
            CONTENT_ATOM.find(block)?.groupValues?.get(1)
                ?: SUMMARY.find(block)?.groupValues?.get(1)
                ?: ""
        } else {
            CONTENT.find(block)?.groupValues?.get(1)
                ?: DESC.find(block)?.groupValues?.get(1)
                ?: ""
        }
        val id = when {
            atom -> textOf(ID_ATOM.find(block)?.groupValues?.get(1))
                .ifBlank { LINK_ATOM.find(block)?.groupValues?.get(1)?.trim().orEmpty() }
            else -> textOf(GUID.find(block)?.groupValues?.get(1))
                .ifBlank { textOf(LINK_RSS.find(block)?.groupValues?.get(1)) }
        }.ifBlank { title }
        return NewsItem(
            id = id.take(120),
            title = title.take(80),
            // Must unwrap CDATA before tag-strip — otherwise `<![CDATA[…]]>` is
            // one giant "tag" match and the article body becomes empty.
            body = textOf(bodyRaw),
        )
    }

    private fun textOf(raw: String?): String {
        if (raw == null) return ""
        var s = raw.trim()
        if (s.startsWith("<![CDATA[", ignoreCase = true) && s.endsWith("]]>")) {
            s = s.substring(9, s.length - 3)
        }
        return HtmlStrip.toPlain(s).trim()
    }
}

object HtmlStrip {
    private val TAG = Regex("(?is)<[^>]+>")
    private val WS = Regex("\\s+")
    private val NUM_ENTITY = Regex("&#(\\d+);")
    private val HEX_ENTITY = Regex("&#x([0-9a-fA-F]+);")

    fun toPlain(html: String): String {
        var s = html
            .replace("&nbsp;", " ", ignoreCase = true)
            .replace("&amp;", "&", ignoreCase = true)
            .replace("&lt;", "<", ignoreCase = true)
            .replace("&gt;", ">", ignoreCase = true)
            .replace("&quot;", "\"", ignoreCase = true)
            .replace("&#34;", "\"", ignoreCase = true)
            .replace("&#39;", "'", ignoreCase = true)
            .replace("&apos;", "'", ignoreCase = true)
        s = NUM_ENTITY.replace(s) { m ->
            m.groupValues[1].toIntOrNull()?.toChar()?.toString() ?: " "
        }
        s = HEX_ENTITY.replace(s) { m ->
            m.groupValues[1].toIntOrNull(16)?.toChar()?.toString() ?: " "
        }
        s = TAG.replace(s, " ")
        s = WS.replace(s, " ").trim()
        return s
    }
}

/**
 * Split plain text into watch-sized pages (fixed line width × line count).
 */
object ArticlePager {
    const val CHARS_PER_LINE = 20
    const val LINES_PER_PAGE = 8

    fun pages(
        text: String,
        charsPerLine: Int = CHARS_PER_LINE,
        linesPerPage: Int = LINES_PER_PAGE,
    ): List<String> {
        val body = text.trim()
        if (body.isEmpty()) return listOf("No text in feed")
        val width = charsPerLine.coerceIn(8, 40)
        val height = linesPerPage.coerceIn(1, 12)
        val lines = wrapLines(body, width)
        val out = mutableListOf<String>()
        var i = 0
        while (i < lines.size) {
            val chunk = lines.subList(i, minOf(i + height, lines.size))
            out += chunk.joinToString("\n")
            i += height
        }
        return out.ifEmpty { listOf("No text in feed") }
    }

    fun wrapLines(text: String, width: Int): List<String> {
        val words = text.split(Regex("\\s+")).filter { it.isNotEmpty() }
        if (words.isEmpty()) return emptyList()
        val lines = mutableListOf<String>()
        var cur = StringBuilder()
        for (w in words) {
            if (w.length > width) {
                if (cur.isNotEmpty()) {
                    lines += cur.toString()
                    cur = StringBuilder()
                }
                var rest = w
                while (rest.length > width) {
                    lines += rest.substring(0, width)
                    rest = rest.substring(width)
                }
                cur.append(rest)
                continue
            }
            if (cur.isEmpty()) {
                cur.append(w)
            } else if (cur.length + 1 + w.length <= width) {
                cur.append(' ').append(w)
            } else {
                lines += cur.toString()
                cur = StringBuilder(w)
            }
        }
        if (cur.isNotEmpty()) lines += cur.toString()
        return lines
    }
}
