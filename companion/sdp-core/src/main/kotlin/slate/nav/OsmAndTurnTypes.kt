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

    /** OsmAnd's own arrival radius is speed-based; 40 m matches walking/city car. */
    const val ARRIVE_RADIUS_M = 40

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

    private val EXIT_RE = Regex(
        """(?:take\s+(\d+)\s+exit|exit\s+(\d+)|(\d+)(?:st|nd|rd|th)?\s+exit)""",
        RegexOption.IGNORE_CASE,
    )

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
            p.contains("roundabout") || p.contains("rotary") || looksLikeExit(p) -> "roundabout"
            isArrivalPhrase(p) -> "arrive"
            p.contains("off route") || p.contains("off-route") -> "off_route"
            p.contains("left") -> "left"
            p.contains("right") -> "right"
            p.contains("straight") || p.contains("continue") || p.contains("ahead") -> "straight"
            else -> "none"
        }
    }

    /**
     * True arrival wording. "Go ahead and arrive at destination" is still
     * approaching — only "arrived" / "reached" mean you are there.
     */
    fun isArrivalPhrase(phrase: String): Boolean {
        val p = phrase.lowercase()
        return p.contains("arrived") ||
            p.contains("destination reached") ||
            p.contains("reached your destination")
    }

    /** OsmAnd voice-router command ids, e.g. `reached_destination`. */
    fun isArrivalVoiceCommand(cmds: List<*>): Boolean =
        cmds.any { cmd ->
            val s = cmd.toString().lowercase()
            s.contains("reached_destination")
        }

    /**
     * OsmAnd has no TurnType for arrival (1–14, Continue=1). Promote the last
     * "go ahead" stretch once remaining distance is inside [ARRIVE_RADIUS_M].
     */
    fun promoteArrive(
        turn: String,
        distanceToTurnM: Int,
        destinationDistanceM: Int,
        sawDestination: Boolean,
    ): String {
        if (turn == "arrive") return "arrive"
        if (!sawDestination) return turn
        if (destinationDistanceM > ARRIVE_RADIUS_M) return turn
        if (turn != "straight" && turn != "none") return turn
        if (distanceToTurnM > 0 && distanceToTurnM + 15 < destinationDistanceM) return turn
        return "arrive"
    }

    fun roundaboutExit(phrase: String): Int {
        val m = EXIT_RE.find(phrase) ?: return 0
        return m.groupValues.drop(1).firstOrNull { it.isNotEmpty() }?.toIntOrNull() ?: 0
    }

    private fun looksLikeExit(p: String): Boolean =
        EXIT_RE.containsMatchIn(p) || (p.contains("take") && p.contains("exit"))
}
