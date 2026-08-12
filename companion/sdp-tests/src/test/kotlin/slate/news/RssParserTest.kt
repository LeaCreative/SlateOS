package slate.news

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class RssParserTest {
    @Test
    fun parsesRssItemsPreferringContentEncoded() {
        val xml = """
            <?xml version="1.0"?>
            <rss version="2.0"><channel>
              <item>
                <title>First &amp; title</title>
                <guid>id-1</guid>
                <description><![CDATA[<p>Short</p>]]></description>
                <content:encoded><![CDATA[<p>Longer <b>body</b> here.</p>]]></content:encoded>
              </item>
              <item>
                <title>Second</title>
                <link>https://example.com/2</link>
                <description>Plain desc</description>
              </item>
            </channel></rss>
        """.trimIndent()
        val items = RssParser.parse(xml, limit = 8)
        assertEquals(2, items.size)
        assertEquals("First & title", items[0].title)
        assertEquals("id-1", items[0].id)
        assertTrue(items[0].body.contains("Longer"))
        assertTrue(!items[0].body.contains("<"))
        assertEquals("Second", items[1].title)
        assertEquals("Plain desc", items[1].body)
    }

    @Test
    fun parsesAtomEntries() {
        val xml = """
            <feed xmlns="http://www.w3.org/2005/Atom">
              <entry>
                <title>Atom one</title>
                <id>tag:ex,1</id>
                <summary>Hello atom</summary>
              </entry>
            </feed>
        """.trimIndent()
        val items = RssParser.parse(xml)
        assertEquals(1, items.size)
        assertEquals("Atom one", items[0].title)
        assertEquals("tag:ex,1", items[0].id)
        assertEquals("Hello atom", items[0].body)
    }

    @Test
    fun respectsLimit() {
        val sb = StringBuilder("<rss><channel>")
        repeat(12) { i ->
            sb.append("<item><title>T$i</title><guid>g$i</guid><description>d</description></item>")
        }
        sb.append("</channel></rss>")
        assertEquals(8, RssParser.parse(sb.toString(), limit = 8).size)
    }
}

class ArticlePagerTest {
    @Test
    fun wrapsAndPages() {
        val text = (1..40).joinToString(" ") { "word$it" }
        val pages = ArticlePager.pages(text, charsPerLine = 20, linesPerPage = 4)
        assertTrue(pages.size >= 2)
        pages.forEach { page ->
            val lines = page.split("\n")
            assertTrue(lines.size <= 4)
            lines.forEach { assertTrue(it.length <= 20, "line too long: $it") }
        }
    }

    @Test
    fun emptyBecomesPlaceholder() {
        assertEquals(listOf("No text in feed"), ArticlePager.pages("   "))
    }
}
