package slate.map

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.min

/**
 * Geographic coordinates to the watch's 240x240 panel, **north always up**.
 *
 * Equirectangular about the viewer, which is the right projection here and not
 * a shortcut: over the few hundred metres a watch face can show, the error
 * against a proper geodesic is far below one pixel, and the alternative
 * (Web Mercator) would buy nothing but a scale factor. There is no rotation
 * term anywhere in this file — north-up is structural, not a setting, so no
 * later change can quietly introduce a heading.
 *
 * Screen convention: x grows east, y grows **south**, so increasing latitude
 * decreases y. That single sign is what puts north at the top.
 */
class MapProjection(
    val centre: GeoPoint,
    /** Metres from the centre to the edge of the inscribed circle (120 px). */
    val radiusM: Double,
) {
    private val metresPerDegLat = 111_132.0

    /**
     * Longitude degrees shrink towards the poles. Taken at the centre latitude
     * and held constant across the screen: over ~500 m the variation is
     * nanometres, and recomputing per point would only add cosines.
     */
    private val metresPerDegLon = 111_320.0 * cos(Math.toRadians(centre.lat))

    private val pxPerMetre = HALF_PX / radiusM

    fun project(p: GeoPoint): DoublePoint {
        val dxMetres = (p.lon - centre.lon) * metresPerDegLon
        val dyMetres = (p.lat - centre.lat) * metresPerDegLat
        return DoublePoint(
            x = HALF_PX + dxMetres * pxPerMetre,
            // Minus: north is up.
            y = HALF_PX - dyMetres * pxPerMetre,
        )
    }

    /** Metres per screen pixel — used to pick a scale-bar length. */
    fun metresPerPixel(): Double = radiusM / HALF_PX

    /**
     * Great-circle-ish distance in metres. Flat-earth over these distances, and
     * only ever used to answer "has the user moved far enough to refetch".
     */
    fun distanceM(a: GeoPoint, b: GeoPoint): Double {
        val dx = (b.lon - a.lon) * metresPerDegLon
        val dy = (b.lat - a.lat) * metresPerDegLat
        return kotlin.math.sqrt(dx * dx + dy * dy)
    }

    data class DoublePoint(val x: Double, val y: Double)

    companion object {
        const val SCREEN_PX = 240
        const val HALF_PX = 120.0

        /**
         * The panel is 240 px but a coordinate is a u8 bounded to 0..239, and
         * `sdp_parser.cpp` rejects the **whole list** on one out-of-range
         * value. Every point that reaches the renderer passes through here.
         */
        const val MAX_COORD = 239

        fun clampCoord(v: Double): Int =
            max(0.0, min(MAX_COORD.toDouble(), v)).toInt()
    }
}

/**
 * Cohen–Sutherland clipping of a polyline against the visible square.
 *
 * A way crossing the viewport can leave and re-enter it, so clipping one input
 * way yields **zero or more** output runs rather than one. Emitting the
 * unclipped point list instead and trusting the firmware to cope is not an
 * option: an off-screen coordinate does not clip, it rejects the list, and the
 * watch keeps whatever was on screen before with no visible reason.
 */
object PolylineClipper {
    private const val INSIDE = 0
    private const val LEFT = 1
    private const val RIGHT = 2
    private const val BOTTOM = 4
    private const val TOP = 8

    private const val MIN = 0.0
    private const val MAX = MapProjection.MAX_COORD.toDouble()

    private fun code(x: Double, y: Double): Int {
        var c = INSIDE
        if (x < MIN) c = c or LEFT else if (x > MAX) c = c or RIGHT
        if (y < MIN) c = c or TOP else if (y > MAX) c = c or BOTTOM
        return c
    }

    /** Clip one segment; null when wholly outside. */
    fun clipSegment(
        x0: Double,
        y0: Double,
        x1: Double,
        y1: Double,
    ): Pair<MapProjection.DoublePoint, MapProjection.DoublePoint>? {
        var ax = x0
        var ay = y0
        var bx = x1
        var by = y1
        var ca = code(ax, ay)
        var cb = code(bx, by)
        var guard = 0
        while (true) {
            // Bounded because each iteration moves an endpoint onto a boundary
            // and clears at least one outcode bit; the guard is belt and braces
            // against a pathological NaN rather than an expected path.
            if (guard++ > 8) return null
            if (ca or cb == INSIDE) {
                return MapProjection.DoublePoint(ax, ay) to MapProjection.DoublePoint(bx, by)
            }
            if (ca and cb != INSIDE) return null
            val out = if (ca != INSIDE) ca else cb
            val nx: Double
            val ny: Double
            when {
                out and BOTTOM != 0 -> {
                    nx = ax + (bx - ax) * (MAX - ay) / (by - ay); ny = MAX
                }
                out and TOP != 0 -> {
                    nx = ax + (bx - ax) * (MIN - ay) / (by - ay); ny = MIN
                }
                out and RIGHT != 0 -> {
                    ny = ay + (by - ay) * (MAX - ax) / (bx - ax); nx = MAX
                }
                else -> {
                    ny = ay + (by - ay) * (MIN - ax) / (bx - ax); nx = MIN
                }
            }
            if (nx.isNaN() || ny.isNaN()) return null
            if (out == ca) {
                ax = nx; ay = ny; ca = code(ax, ay)
            } else {
                bx = nx; by = ny; cb = code(bx, by)
            }
        }
    }

    /**
     * Clip a projected way into visible runs.
     *
     * Consecutive segments that stay inside are joined into one run so they
     * cost one POLYLINE op rather than one per segment.
     */
    fun clipPolyline(points: List<MapProjection.DoublePoint>): List<List<MapProjection.DoublePoint>> {
        if (points.size < 2) return emptyList()
        val runs = mutableListOf<List<MapProjection.DoublePoint>>()
        var current = mutableListOf<MapProjection.DoublePoint>()
        for (i in 0 until points.size - 1) {
            val a = points[i]
            val b = points[i + 1]
            val clipped = clipSegment(a.x, a.y, b.x, b.y)
            if (clipped == null) {
                if (current.size >= 2) runs += current
                current = mutableListOf()
                continue
            }
            val (ca, cb) = clipped
            if (current.isEmpty()) {
                current += ca
            } else if (!near(current.last(), ca)) {
                // The line left the box and came back: start a new run rather
                // than drawing a chord across the gap.
                if (current.size >= 2) runs += current
                current = mutableListOf(ca)
            }
            current += cb
        }
        if (current.size >= 2) runs += current
        return runs
    }

    private fun near(a: MapProjection.DoublePoint, b: MapProjection.DoublePoint): Boolean =
        abs(a.x - b.x) < 0.001 && abs(a.y - b.y) < 0.001
}
