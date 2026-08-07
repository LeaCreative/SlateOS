package slate.map

import slate.interpreter.DisplayListInterpreter
import slate.parse.SdpStatus
import slate.render.Rgb565
import kotlin.math.abs
import kotlin.random.Random
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * The map pipeline: projection, clipping, budget.
 *
 * The one that must never fail is [everyRenderedListIsAcceptedByTheParser].
 * `sdp_parser.cpp` rejects the **whole display list** on a single coordinate
 * outside 0..239 — not the offending op, the whole list. A clipping bug
 * therefore does not show up as a stray line; it shows up as the map silently
 * never appearing, with the previous screen left in place and `dl_rej`
 * ticking up on an overlay the operator has to photograph to read.
 */
class MapPipelineTest {

    private val seychelles = GeoPoint(-4.6796, 55.4920)

    private fun way(cls: MapClass, vararg pts: Pair<Double, Double>) =
        MapWay(cls, pts.map { GeoPoint(it.first, it.second) })

    // ── Projection ───────────────────────────────────────────────────────────

    @Test
    fun centreProjectsToTheCentreOfTheScreen() {
        val p = MapProjection(seychelles, 400.0).project(seychelles)
        assertEquals(120.0, p.x, 0.001)
        assertEquals(120.0, p.y, 0.001)
    }

    /**
     * North up, stated as a test rather than a comment: increasing latitude
     * must decrease y. If this ever inverts, the map is upside down and every
     * other test still passes.
     */
    @Test
    fun northIsUp() {
        val proj = MapProjection(seychelles, 400.0)
        val north = proj.project(GeoPoint(seychelles.lat + 0.001, seychelles.lon))
        val east = proj.project(GeoPoint(seychelles.lat, seychelles.lon + 0.001))
        assertTrue(north.y < 120.0, "north of centre must be above it, got y=${north.y}")
        assertEquals(120.0, north.x, 0.001, "due north must not shift east or west")
        assertTrue(east.x > 120.0, "east of centre must be right of it, got x=${east.x}")
        assertEquals(120.0, east.y, 0.001, "due east must not shift north or south")
    }

    @Test
    fun radiusMapsToTheEdgeOfTheInscribedCircle() {
        val radius = 400.0
        val proj = MapProjection(seychelles, radius)
        // 400 m due north of centre should land on the top edge, y = 0.
        val degLat = radius / 111_132.0
        val p = proj.project(GeoPoint(seychelles.lat + degLat, seychelles.lon))
        assertEquals(0.0, p.y, 0.5)
    }

    // ── Clipping ─────────────────────────────────────────────────────────────

    @Test
    fun aWayCrossingTheScreenIsClippedToTheViewport() {
        val runs = PolylineClipper.clipPolyline(
            listOf(
                MapProjection.DoublePoint(-500.0, 120.0),
                MapProjection.DoublePoint(700.0, 120.0),
            ),
        )
        assertEquals(1, runs.size)
        val run = runs.first()
        assertEquals(0.0, run.first().x, 0.001)
        assertEquals(239.0, run.last().x, 0.001)
    }

    @Test
    fun aWayEntirelyOffScreenProducesNothing() {
        val runs = PolylineClipper.clipPolyline(
            listOf(
                MapProjection.DoublePoint(-500.0, -500.0),
                MapProjection.DoublePoint(-400.0, -450.0),
            ),
        )
        assertTrue(runs.isEmpty())
    }

    /**
     * A way that leaves the viewport and comes back must produce two runs, not
     * one. Joining them would draw a straight chord across the screen — a road
     * that does not exist, which is worse than a missing one.
     */
    @Test
    fun aWayThatLeavesAndReturnsBecomesTwoRuns() {
        val runs = PolylineClipper.clipPolyline(
            listOf(
                MapProjection.DoublePoint(100.0, 100.0),
                MapProjection.DoublePoint(400.0, 100.0),
                MapProjection.DoublePoint(400.0, 150.0),
                MapProjection.DoublePoint(100.0, 150.0),
            ),
        )
        assertEquals(2, runs.size, "expected two visible runs, got ${runs.size}")
    }

    // ── Budget ───────────────────────────────────────────────────────────────

    /**
     * The whole point of the renderer. Absurd input, hard cap held.
     */
    @Test
    fun budgetIsNeverExceededNoMatterHowMuchDataArrives() {
        val rng = Random(7)
        val ways = (1 until 4000).map {
            val cls = MapClass.entries[rng.nextInt(MapClass.entries.size)]
            MapWay(
                cls,
                (0 until 40).map {
                    GeoPoint(
                        seychelles.lat + (rng.nextDouble() - 0.5) * 0.01,
                        seychelles.lon + (rng.nextDouble() - 0.5) * 0.01,
                    )
                },
            )
        }
        val data = MapData(seychelles, 400.0, ways, 0L)
        for (budget in listOf(512, 1024, 2048, 4096)) {
            val r = MapRenderer.render(data, seychelles, 400.0, budgetBytes = budget)
            assertTrue(
                r.bytes.size <= budget,
                "budget $budget exceeded: produced ${r.bytes.size} B",
            )
            assertTrue(r.waysDropped > 0, "expected drops at budget $budget with 4000 ways")
        }
    }

    /**
     * When the budget bites, the trunk road survives and the footpath does not.
     * A map that drops by arrival order rather than importance is the failure
     * this ordering exists to prevent.
     */
    @Test
    fun importantWaysSurviveTheBudgetAndTrivialOnesDoNot() {
        // One motorway, then far more footpaths than can possibly fit.
        val ways = mutableListOf(
            way(
                MapClass.Motorway,
                seychelles.lat - 0.003 to seychelles.lon,
                seychelles.lat + 0.003 to seychelles.lon,
            ),
        )
        repeat(400) { i ->
            ways += way(
                MapClass.Path,
                seychelles.lat - 0.002 to seychelles.lon - 0.002 + i * 0.00001,
                seychelles.lat + 0.002 to seychelles.lon - 0.002 + i * 0.00001,
            )
        }
        val data = MapData(seychelles, 400.0, ways, 0L)
        val r = MapRenderer.render(data, seychelles, 400.0, budgetBytes = 700)

        val interpreted = DisplayListInterpreter().render(r.bytes)
        assertTrue(r.waysDropped > 0, "expected the budget to bite")
        // The motorway is white (PAL_MAJOR); paths are grey. If the motorway
        // had been dropped there would be no white pixels on the road area.
        val white = Rgb565.toArgb(0xFFFF)
        assertTrue(
            interpreted.framebuffer.pixels.any { it == white },
            "the motorway was dropped while footpaths were kept",
        )
    }

    /**
     * Guards the invariant the whole map depends on. A rejected list is
     * invisible on the watch, so this failing in CI is far cheaper than the
     * alternative.
     */
    @Test
    fun everyRenderedListIsAcceptedByTheParser() {
        val rng = Random(99)
        repeat(40) { iteration ->
            // Ways deliberately sprayed far outside the view, so clipping is
            // doing real work rather than being a no-op on tidy input.
            val ways = (0 until 120).map {
                MapWay(
                    MapClass.entries[rng.nextInt(MapClass.entries.size)],
                    (0 until 2 + rng.nextInt(30)).map {
                        GeoPoint(
                            seychelles.lat + (rng.nextDouble() - 0.5) * 0.4,
                            seychelles.lon + (rng.nextDouble() - 0.5) * 0.4,
                        )
                    },
                )
            }
            val radius = 100.0 + rng.nextDouble() * 1500.0
            val data = MapData(seychelles, radius, ways, 0L)
            val r = MapRenderer.render(data, seychelles, radius)
            val out = DisplayListInterpreter().render(r.bytes)
            assertEquals(
                SdpStatus.Ok,
                out.status,
                "iteration $iteration: parser did not accept the list (radius=$radius)",
            )
        }
    }

    @Test
    fun emptyDataStillProducesAValidScreen() {
        val data = MapData(seychelles, 400.0, emptyList(), 0L)
        val r = MapRenderer.render(data, seychelles, 400.0)
        val out = DisplayListInterpreter().render(r.bytes)
        assertEquals(SdpStatus.Ok, out.status)
        assertEquals(0, r.waysDrawn)
        // Chrome must still be there: without the marker the user has no idea
        // where they are on an empty map.
        assertTrue(r.bytes.size > 20, "expected chrome even with no ways")
    }

    // ── Simplification ───────────────────────────────────────────────────────

    @Test
    fun simplificationKeepsTheShapeAndDropsCollinearPoints() {
        val straight = (0..50).map { MapProjection.DoublePoint(it * 4.0, 100.0) }
        val simplified = MapSimplify.douglasPeucker(straight, 1.2)
        assertEquals(2, simplified.size, "a straight line needs only its endpoints")

        val corner = listOf(
            MapProjection.DoublePoint(0.0, 0.0),
            MapProjection.DoublePoint(50.0, 0.0),
            MapProjection.DoublePoint(100.0, 100.0),
        )
        assertEquals(3, MapSimplify.douglasPeucker(corner, 1.2).size, "a real corner must survive")
    }

    @Test
    fun scaleBarUsesRoundNumbers() {
        assertEquals(100, MapRenderer.niceDistance(123.0))
        assertEquals(200, MapRenderer.niceDistance(240.0))
        assertEquals(500, MapRenderer.niceDistance(700.0))
        assertEquals(1000, MapRenderer.niceDistance(1400.0))
        assertTrue(MapRenderer.niceDistance(0.0) > 0, "must never return zero")
    }

    /**
     * Reprojecting cached ways around a moved viewer is what makes the map
     * track between fetches. If it did not move, the map would be pinned to
     * the fetch point and lurch.
     */
    @Test
    fun movingTheViewerMovesTheMapWithoutRefetching() {
        val ways = listOf(
            way(
                MapClass.Primary,
                seychelles.lat to seychelles.lon - 0.002,
                seychelles.lat to seychelles.lon + 0.002,
            ),
        )
        val data = MapData(seychelles, 400.0, ways, 0L)
        val here = MapRenderer.render(data, seychelles, 400.0)
        val movedNorth = MapRenderer.render(
            data,
            GeoPoint(seychelles.lat + 0.0009, seychelles.lon),
            400.0,
        )
        assertTrue(
            !here.bytes.contentEquals(movedNorth.bytes),
            "the same data at a different viewer position produced an identical screen",
        )
        assertEquals(SdpStatus.Ok, DisplayListInterpreter().render(movedNorth.bytes).status)
    }

    @Test
    fun classificationMapsTheTagsThatMatter() {
        assertEquals(MapClass.Motorway, MapClass.fromTags("motorway", null, null))
        assertEquals(MapClass.Trunk, MapClass.fromTags("trunk_link", null, null))
        assertEquals(MapClass.Water, MapClass.fromTags(null, "river", null))
        assertEquals(MapClass.Rail, MapClass.fromTags(null, null, "tram"))
        // An unknown highway value is still a road worth drawing if budget
        // remains — OSM's vocabulary changes without notice.
        assertEquals(MapClass.Path, MapClass.fromTags("busway", null, null))
        assertEquals(null, MapClass.fromTags(null, null, null))
        assertEquals(null, MapClass.fromTags(null, "dock", null))
    }

    private fun assertEquals(expected: Double, actual: Double, tolerance: Double, message: String = "") {
        assertTrue(
            abs(expected - actual) <= tolerance,
            "$message expected $expected±$tolerance got $actual",
        )
    }
}
