package slate.map

import slate.dsl.DisplayListBuilder
import slate.wire.Align
import slate.wire.SdpColor
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb

/**
 * Vector map to SDP display list, inside a hard byte budget.
 *
 * The budget is the design, not a check at the end. `docs/subapp-rules.md` §2
 * caps a list at 4096 B in the parser and ~2048 B in practice, because the
 * credit window (§3) drops larger screens intermittently. A map has unbounded
 * source detail, so something is always getting dropped; the only question is
 * whether it is chosen or arbitrary.
 *
 * So ways are emitted **most important first** and the loop stops when the next
 * one would breach the budget. Dropping a footpath to keep a trunk road is a
 * map. Truncating the byte stream wherever it happens to land is not.
 */
object MapRenderer {

    /** Palette slots, fixed so the renderer and its tests agree. */
    private const val PAL_BG = 0
    private const val PAL_MAJOR = 1
    private const val PAL_MINOR = 2
    private const val PAL_WATER = 3
    private const val PAL_MARKER = 4
    private const val PAL_CHROME = 5
    private const val PAL_BUILDING = 6

    /**
     * POLYLINE carries a u8 point count, and each point is two bytes, so one op
     * can hold 255 points. Capped far lower: a single way is not worth half the
     * screen's budget, and a long way past this is split rather than truncated.
     */
    const val MAX_POLYLINE_POINTS = 60

    /** op + count + colour tag + width, then 2 bytes per point. */
    private const val POLYLINE_FIXED_BYTES = 4

    /**
     * Buildings are judged visible at 2 px rather than the road threshold: at
     * 100 m radius a 12 m house is about 14 px, but a shed is 3, and dropping
     * every small structure leaves a street of gaps.
     */
    private const val BUILDING_MIN_EXTENT_P = 2

    data class Result(
        val bytes: ByteArray,
        /** Ways that reached the screen. */
        val waysDrawn: Int,
        /** Ways dropped because the budget ran out — the honest failure count. */
        val waysDropped: Int,
        val scaleBarMetres: Int,
    ) {
        override fun equals(other: Any?): Boolean =
            other is Result && bytes.contentEquals(other.bytes)

        override fun hashCode(): Int = bytes.contentHashCode()
    }

    /**
     * Render [data] around [viewer].
     *
     * [viewer] is deliberately separate from `data.centre`: the cached ways were
     * fetched around wherever the user was at the time, and reprojecting them
     * around a position that has since moved is free. Only refetching costs
     * network, so the map keeps tracking between fetches.
     */
    fun render(
        data: MapData,
        viewer: GeoPoint,
        radiusM: Double,
        budgetBytes: Int = 2048,
        simplifyToleranceP: Double = 1.2,
        minExtentP: Int = 3,
    ): Result {
        val projection = MapProjection(viewer, radiusM)

        // Project, clip and simplify everything first, then decide what fits.
        // Doing it in this order means the budget is spent on ways as they will
        // actually be drawn, not on their pre-simplification size.
        val candidates = ArrayList<Pair<MapClass, List<ScreenPoint>>>()
        for (way in data.ways) {
            val projected = way.points.map { projection.project(it) }
            for (run in PolylineClipper.clipPolyline(projected)) {
                val simplified = MapSimplify.douglasPeucker(run, simplifyToleranceP)
                val pixels = MapSimplify.dedupePixels(
                    simplified.map {
                        ScreenPoint(
                            MapProjection.clampCoord(it.x),
                            MapProjection.clampCoord(it.y),
                        )
                    },
                )
                if (pixels.size < 2) continue
                // A building is a closed outline a few pixels across, so the
                // extent test that usefully drops a stub road would drop nearly
                // all of them. Judge them on a smaller threshold.
                val extent = if (way.cls == MapClass.Building) {
                    minExtentP.coerceAtMost(BUILDING_MIN_EXTENT_P)
                } else {
                    minExtentP
                }
                if (!MapSimplify.isVisiblySized(pixels, extent)) continue
                for (chunk in chunkPolyline(pixels)) {
                    candidates += way.cls to chunk
                }
            }
        }
        candidates.sortBy { it.first.rank }

        val b = DisplayListBuilder()
        b.palette(PAL_BG, rgb(0x0000))       // black
        b.palette(PAL_MAJOR, rgb(0xFFFF))    // white — roads you would name
        b.palette(PAL_MINOR, rgb(0x6B4D))    // grey — everything else
        b.palette(PAL_WATER, rgb(0x04FF))    // blue
        b.palette(PAL_MARKER, rgb(0xF9E0))   // amber — you
        b.palette(PAL_CHROME, rgb(0x9CD3))   // dim — north mark and scale
        // Buildings sit below the minor-road grey on purpose: they are context
        // you read past, not routes you follow. Same reason they rank last.
        b.palette(PAL_BUILDING, rgb(0x39C7))
        b.clear(pal(PAL_BG))

        // Chrome is drawn first and its cost is reserved, so the scale bar and
        // the position marker cannot be squeezed out by road detail. A map with
        // no "you are here" is a wallpaper.
        val chromeBytes = chromeCost(projection)
        val wayBudget = budgetBytes - chromeBytes - b.toByteArray().size

        var used = 0
        var drawn = 0
        var dropped = 0
        for ((cls, points) in candidates) {
            val cost = POLYLINE_FIXED_BYTES + points.size * 2
            if (used + cost > wayBudget) {
                dropped++
                continue
            }
            b.polyline(
                points.map { it.x to it.y },
                colourFor(cls),
                cls.width,
            )
            used += cost
            drawn++
        }

        val scaleBar = drawChrome(b, projection)

        val bytes = b.toByteArray()
        return Result(
            bytes = bytes,
            waysDrawn = drawn,
            waysDropped = dropped,
            scaleBarMetres = scaleBar,
        )
    }

    private fun colourFor(cls: MapClass): SdpColor = when (cls) {
        MapClass.Motorway, MapClass.Trunk, MapClass.Primary, MapClass.Secondary ->
            pal(PAL_MAJOR)
        MapClass.Water, MapClass.Coastline -> pal(PAL_WATER)
        MapClass.Building -> pal(PAL_BUILDING)
        else -> pal(PAL_MINOR)
    }

    /**
     * Split an over-long way so no single POLYLINE exceeds the point cap.
     *
     * Chunks overlap by one point, otherwise the join between them is a visible
     * gap in a road.
     */
    private fun chunkPolyline(points: List<ScreenPoint>): List<List<ScreenPoint>> {
        if (points.size <= MAX_POLYLINE_POINTS) return listOf(points)
        val out = mutableListOf<List<ScreenPoint>>()
        var start = 0
        while (start < points.size - 1) {
            val end = minOf(start + MAX_POLYLINE_POINTS, points.size)
            out += points.subList(start, end)
            start = end - 1
        }
        return out
    }

    /**
     * Bytes [drawChrome] will need. Kept next to it: if one changes and the
     * other does not, the map silently overshoots its budget.
     */
    private fun chromeCost(projection: MapProjection): Int {
        // marker circle 7 + ring 7 + north 'N' TEXT_SCALED ~14 + scale line 7
        // + scale label TEXT_SCALED ~17 + COMMIT 2, rounded up for slack.
        return 64
    }

    /** Returns the scale-bar length in metres, for the caller's status line. */
    private fun drawChrome(b: DisplayListBuilder, projection: MapProjection): Int {
        // North mark. There is no compass in this app — north is up because the
        // projection has no rotation term, so this label is always true.
        b.textScaled(
            font = 1, x = 120, y = 2, align = Align.CENTER,
            color = pal(PAL_CHROME), scale = 2, text = "N",
        )

        // The viewer, dead centre by construction.
        b.circle(120, 120, 4, pal(PAL_MARKER), Style.FILL)
        b.circle(120, 120, 7, pal(PAL_MARKER), Style.STROKE)

        // Scale bar, bottom left. Without it the map has no absolute size and
        // a 100 m view is indistinguishable from a 1 km one.
        val mPerPx = projection.metresPerPixel()
        val target = niceDistance(60 * mPerPx)
        val barPx = (target / mPerPx).toInt().coerceIn(20, 110)
        val y = 228
        b.line(12, y, 12 + barPx, y, pal(PAL_CHROME), 2)
        b.textScaled(
            font = 1, x = 12, y = y - 18, align = Align.LEFT,
            color = pal(PAL_CHROME), scale = 1,
            text = if (target >= 1000) "${(target / 1000)}km" else "${target}m",
        )
        b.commit()
        return target
    }

    /** Round a distance to something a human reads: 1/2/5 x 10^n. */
    fun niceDistance(raw: Double): Int {
        if (raw <= 0.0) return 10
        var unit = 1.0
        while (unit * 10 <= raw) unit *= 10
        val n = raw / unit
        val mult = when {
            n >= 5 -> 5.0
            n >= 2 -> 2.0
            else -> 1.0
        }
        return (unit * mult).toInt().coerceAtLeast(1)
    }
}
