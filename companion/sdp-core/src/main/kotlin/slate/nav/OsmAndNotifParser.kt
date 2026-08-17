package slate.nav

/**
 * Parse OsmAnd's navigation notification into metres + turn token.
 *
 * OsmAnd NavigationNotification sets:
 * - title: `"<nextTurnDist> • <turn phrase>"`
 * - big text: street line(s), then `"<leftDistance> • <time> • <eta> [• speed]"`
 */
object OsmAndNotifParser {
    data class Parsed(
        val turn: String,
        val distanceToTurnM: Int,
        val destinationDistanceM: Int,
        val street: String,
    )

    private val DIST_RE =
        Regex("""(\d+(?:[.,]\d+)?)\s*(km|m|mi|yd|ft)\b""", RegexOption.IGNORE_CASE)

    fun isOsmAndPackage(pkg: String): Boolean =
        pkg == "net.osmand" ||
            pkg == "net.osmand.plus" ||
            pkg.startsWith("net.osmand.")

    fun parse(title: String, text: String): Parsed? {
        if (title.isBlank() && text.isBlank()) return null
        val turnDist = firstDistanceMetres(title)
        val turnPhrase = title.substringAfter('•', missingDelimiterValue = "").trim()
            .ifEmpty { title }
        val turn = if (turnPhrase.isNotEmpty()) {
            OsmAndTurnTypes.fromPhrase(turnPhrase)
        } else {
            "none"
        }
        val street = text.lineSequence()
            .map { it.trim() }
            .firstOrNull { it.isNotEmpty() && !looksLikeSummaryLine(it) }
            .orEmpty()
        val dest = text.lineSequence()
            .map { it.trim() }
            .lastOrNull { looksLikeSummaryLine(it) }
            ?.let { firstDistanceMetres(it) }
            ?: 0
        if (turnDist <= 0 && dest <= 0 && turn == "none") return null
        return Parsed(
            turn = turn,
            distanceToTurnM = turnDist.coerceAtLeast(0),
            destinationDistanceM = dest.coerceAtLeast(0),
            street = street.take(48),
        )
    }

    private fun looksLikeSummaryLine(line: String): Boolean =
        line.contains('•') && DIST_RE.containsMatchIn(line)

    fun firstDistanceMetres(s: String): Int {
        val m = DIST_RE.find(s) ?: return 0
        val raw = m.groupValues[1].replace(',', '.')
        val n = raw.toDoubleOrNull() ?: return 0
        return when (m.groupValues[2].lowercase()) {
            "km" -> (n * 1000.0).toInt()
            "mi" -> (n * 1609.34).toInt()
            "yd" -> (n * 0.9144).toInt()
            "ft" -> (n * 0.3048).toInt()
            else -> n.toInt()
        }
    }
}
