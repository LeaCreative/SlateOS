package slate.nav

import org.json.JSONObject

/**
 * Generic navigation maneuver — OsmAnd and other sources map into this shape.
 * Host posts JSON to the focused nav sub-app as `onEvent('nav', …)`.
 *
 * [turn] is always relative to **direction of travel** (route course), never
 * phone compass orientation — that is what OsmAnd's TurnType values mean.
 */
data class NavManeuver(
    /** left | right | slight_left | slight_right | sharp_left | sharp_right |
     *  keep_left | keep_right | straight | u_turn | roundabout | arrive |
     *  off_route | none */
    val turn: String,
    /** Distance to next maneuver, metres. */
    val distanceM: Int,
    val street: String,
    /** Route progress 0–100 (optional; 0 if unknown). */
    val progressPct: Int,
    /** ETA as unix epoch seconds, or 0 if unknown. */
    val etaEpochSec: Long,
    /** Remaining route distance to destination, metres (0 if unknown). */
    val destinationDistanceM: Int = 0,
    /** Roundabout exit number, 0 if not a numbered exit. */
    val roundaboutExit: Int = 0,
    /** lost_gps | disconnected | ok */
    val status: String = "ok",
) {
    fun toJson(): String = JSONObject()
        .put("type", "maneuver")
        .put("turn", turn)
        .put("distanceM", distanceM)
        .put("street", street)
        .put("progressPct", progressPct.coerceIn(0, 100))
        .put("etaEpochSec", etaEpochSec)
        .put("destinationDistanceM", destinationDistanceM.coerceAtLeast(0))
        .put("roundaboutExit", roundaboutExit.coerceAtLeast(0))
        .put("status", status)
        .toString()

    companion object {
        fun lostGps(last: NavManeuver?): NavManeuver =
            (last ?: idle()).copy(status = "lost_gps", street = last?.street ?: "GPS lost")

        fun disconnected(last: NavManeuver?): NavManeuver =
            (last ?: idle()).copy(status = "disconnected", street = "Phone disconnected")

        fun idle(): NavManeuver = NavManeuver(
            turn = "none",
            distanceM = 0,
            street = "Waiting for OsmAnd",
            progressPct = 0,
            etaEpochSec = 0,
            destinationDistanceM = 0,
            status = "ok",
        )

        fun demo(kind: String): NavManeuver {
            val turn = when (kind.lowercase()) {
                "right" -> "right"
                "slight_right" -> "slight_right"
                "slight_left" -> "slight_left"
                "u", "u_turn" -> "u_turn"
                "arrive" -> "arrive"
                "straight" -> "straight"
                "roundabout" -> "roundabout"
                else -> "left"
            }
            val arrived = turn == "arrive"
            return NavManeuver(
                turn = turn,
                distanceM = if (arrived) 0 else 250,
                street = if (arrived) "" else "Demo St",
                progressPct = if (arrived) 100 else 42,
                etaEpochSec = if (arrived) 0L else System.currentTimeMillis() / 1000L + 12 * 60,
                destinationDistanceM = if (arrived) 0 else 5200,
                status = "ok",
            )
        }
    }
}
