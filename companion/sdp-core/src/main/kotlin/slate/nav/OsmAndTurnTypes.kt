package slate.nav

/**
 * OsmAnd TurnType integer values → Slate turn tokens.
 * Values are relative to direction of travel, not device orientation.
 */
object OsmAndTurnTypes {
    const val C = 1
    const val TL = 2
    const val TSLL = 3
    const val TSHL = 4
    const val TR = 5
    const val TSLR = 6
    const val TSHR = 7
    const val KL = 8
    const val KR = 9
    const val TU = 10
    const val TRU = 11
    const val OFFR = 12
    const val RNDB = 13
    const val RNLB = 14

    fun toTurn(value: Int): String = when (value) {
        C -> "straight"
        TL -> "left"
        TSLL -> "slight_left"
        TSHL -> "sharp_left"
        TR -> "right"
        TSLR -> "slight_right"
        TSHR -> "sharp_right"
        KL -> "keep_left"
        KR -> "keep_right"
        TU, TRU -> "u_turn"
        OFFR -> "off_route"
        RNDB, RNLB -> "roundabout"
        else -> "none"
    }

    fun fromPhrase(phrase: String): String {
        val p = phrase.lowercase()
        return when {
            p.contains("u-turn") || p.contains("u turn") || p.contains("uturn") -> "u_turn"
            p.contains("sharp left") -> "sharp_left"
            p.contains("sharp right") -> "sharp_right"
            p.contains("slight left") || p.contains("bear left") -> "slight_left"
            p.contains("slight right") || p.contains("bear right") -> "slight_right"
            p.contains("keep left") -> "keep_left"
            p.contains("keep right") -> "keep_right"
            p.contains("roundabout") || p.contains("rotary") -> "roundabout"
            p.contains("arrive") || p.contains("destination") -> "arrive"
            p.contains("off route") || p.contains("off-route") -> "off_route"
            p.contains("left") -> "left"
            p.contains("right") -> "right"
            p.contains("straight") || p.contains("continue") || p.contains("ahead") -> "straight"
            else -> "none"
        }
    }
}
