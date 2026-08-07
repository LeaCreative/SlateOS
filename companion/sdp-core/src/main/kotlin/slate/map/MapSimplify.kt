package slate.map

import kotlin.math.abs
import kotlin.math.hypot

/**
 * Point reduction, in screen space rather than geographic space.
 *
 * OSM geometry carries survey-grade detail — a roundabout can be thirty nodes.
 * At 240x240 with roughly two bytes per point, that roundabout is 60 bytes of a
 * 2 KB budget spent on a circle six pixels across. Simplifying in pixels rather
 * than degrees means the tolerance means the same thing everywhere on Earth and
 * at every zoom, which a tolerance in degrees does not.
 */
object MapSimplify {

    /**
     * Douglas–Peucker.
     *
     * Iterative, not recursive: a pathological way could otherwise recurse
     * once per point, and this runs inside the compositor path where a
     * StackOverflowError would take out the link service rather than one screen.
     */
    fun douglasPeucker(
        points: List<MapProjection.DoublePoint>,
        toleranceP: Double,
    ): List<MapProjection.DoublePoint> {
        if (points.size <= 2) return points
        val keep = BooleanArray(points.size)
        keep[0] = true
        keep[points.size - 1] = true

        val stack = ArrayDeque<Pair<Int, Int>>()
        stack.addLast(0 to points.size - 1)
        while (stack.isNotEmpty()) {
            val (first, last) = stack.removeLast()
            if (last <= first + 1) continue
            var maxDist = 0.0
            var index = first
            for (i in first + 1 until last) {
                val d = perpendicularDistance(points[i], points[first], points[last])
                if (d > maxDist) {
                    maxDist = d
                    index = i
                }
            }
            if (maxDist > toleranceP) {
                keep[index] = true
                stack.addLast(first to index)
                stack.addLast(index to last)
            }
        }
        return points.filterIndexed { i, _ -> keep[i] }
    }

    private fun perpendicularDistance(
        p: MapProjection.DoublePoint,
        a: MapProjection.DoublePoint,
        b: MapProjection.DoublePoint,
    ): Double {
        val dx = b.x - a.x
        val dy = b.y - a.y
        if (dx == 0.0 && dy == 0.0) return hypot(p.x - a.x, p.y - a.y)
        val t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy)
        val clamped = t.coerceIn(0.0, 1.0)
        val projX = a.x + clamped * dx
        val projY = a.y + clamped * dy
        return hypot(p.x - projX, p.y - projY)
    }

    /**
     * Drop points that land on the same pixel as their predecessor.
     *
     * Runs after rounding to integers. Two points a third of a pixel apart are
     * two identical coordinates on the wire — four bytes that draw nothing.
     */
    fun dedupePixels(points: List<ScreenPoint>): List<ScreenPoint> {
        if (points.size < 2) return points
        val out = ArrayList<ScreenPoint>(points.size)
        out += points.first()
        for (i in 1 until points.size) {
            val prev = out.last()
            val p = points[i]
            if (p.x != prev.x || p.y != prev.y) out += p
        }
        return out
    }

    /**
     * Is this way worth any bytes at all?
     *
     * A way whose whole extent is under a few pixels is a smudge; at 2 KB the
     * bytes buy more elsewhere. Measured on the bounding box rather than the
     * path length, so a tight zigzag that goes nowhere is dropped too.
     */
    fun isVisiblySized(points: List<ScreenPoint>, minExtentP: Int): Boolean {
        if (points.size < 2) return false
        var minX = Int.MAX_VALUE
        var maxX = Int.MIN_VALUE
        var minY = Int.MAX_VALUE
        var maxY = Int.MIN_VALUE
        for (p in points) {
            if (p.x < minX) minX = p.x
            if (p.x > maxX) maxX = p.x
            if (p.y < minY) minY = p.y
            if (p.y > maxY) maxY = p.y
        }
        return abs(maxX - minX) >= minExtentP || abs(maxY - minY) >= minExtentP
    }
}
