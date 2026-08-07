package slate.map

import slate.interpreter.DisplayListInterpreter
import slate.parse.SdpStatus
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/**
 * The whole pipeline against a **real** Overpass response.
 *
 * `victoria.json` is 195 KB of genuine OSM data — 269 ways around Victoria,
 * Mahé, captured 7 Aug 2026 and checked in. Synthetic data is tidy in ways real
 * OSM is not: ways that double back, service roads stacked on top of each
 * other, a stream that wanders off the screen and returns. Those are the shapes
 * that break clipping.
 *
 * It also states the actual problem in one number: **195 KB in, 2048 B out.**
 */
class MapFixtureTest {

    private val victoria = GeoPoint(-4.6191, 55.4513)

    private fun fixture(): String =
        checkNotNull(javaClass.getResourceAsStream("/map/victoria.json")) {
            "missing test resource /map/victoria.json"
        }.bufferedReader().readText()

    private fun ways(): List<MapWay> = OverpassParser.parse(fixture())

    @Test
    fun realOverpassOutputParses() {
        val ways = ways()
        assertTrue(ways.size > 100, "expected a few hundred ways, got ${ways.size}")
        assertTrue(
            ways.all { it.points.size >= 2 },
            "a way with fewer than two points reached the renderer",
        )
        // The mix that makes the importance ordering worth having.
        val classes = ways.map { it.cls }.toSet()
        assertTrue(MapClass.Primary in classes, "expected primary roads in Victoria")
        assertTrue(MapClass.Service in classes, "expected service roads")
        assertTrue(MapClass.Water in classes, "expected the streams")
    }

    /**
     * The headline claim, asserted rather than asserted-in-prose: real data at
     * every radius the setting allows produces a list the parser accepts and
     * the credit window will carry.
     */
    @Test
    fun realDataRendersInsideBudgetAtEveryRadius() {
        val ways = ways()
        for (radius in listOf(100.0, 200.0, 400.0, 800.0, 1200.0, 2000.0)) {
            val data = MapData(victoria, radius, ways, 0L)
            val r = MapRenderer.render(data, victoria, radius)
            assertTrue(
                r.bytes.size <= 2048,
                "radius ${radius}m produced ${r.bytes.size} B",
            )
            val out = DisplayListInterpreter().render(r.bytes)
            assertEquals(
                SdpStatus.Ok,
                out.status,
                "radius ${radius}m: parser did not accept the list",
            )
            assertTrue(
                r.waysDrawn > 0,
                "radius ${radius}m drew nothing at all",
            )
        }
    }

    /**
     * Walking around does not break it. Every offset must still clip cleanly —
     * this is the case that actually happens in use, and the one where a
     * clipping bug would surface as the map vanishing mid-walk.
     */
    @Test
    fun everyViewerOffsetStillProducesAnAcceptedList() {
        val ways = ways()
        val data = MapData(victoria, 400.0, ways, 0L)
        var drewSomething = 0
        for (dLat in -6..6) {
            for (dLon in -6..6) {
                val viewer = GeoPoint(
                    victoria.lat + dLat * 0.0008,
                    victoria.lon + dLon * 0.0008,
                )
                val r = MapRenderer.render(data, viewer, 400.0)
                assertTrue(r.bytes.size <= 2048, "offset $dLat,$dLon: ${r.bytes.size} B")
                val out = DisplayListInterpreter().render(r.bytes)
                assertEquals(
                    SdpStatus.Ok,
                    out.status,
                    "offset $dLat,$dLon: parser rejected the list",
                )
                if (r.waysDrawn > 0) drewSomething++
            }
        }
        assertTrue(drewSomething > 100, "expected most offsets to still show roads")
    }

    /**
     * A garbled response must degrade to an empty map, not an exception on the
     * compositor path. This is third-party data over a mobile network.
     */
    @Test
    fun malformedResponsesDegradeToNothing() {
        assertEquals(0, OverpassParser.parse("""{"elements":[]}""").size)
        assertEquals(0, OverpassParser.parse("""{"nonsense":true}""").size)
        // Ways with unusable geometry are skipped, not fatal.
        assertEquals(
            0,
            OverpassParser.parse(
                """{"elements":[
                     {"type":"way","tags":{"highway":"primary"},"geometry":[{"lat":1.0}]},
                     {"type":"way","tags":{"highway":"primary"}},
                     {"type":"node","lat":1.0,"lon":2.0}
                   ]}""",
            ).size,
        )
        // Out-of-range coordinates are dropped rather than cast into a u8.
        assertEquals(
            0,
            OverpassParser.parse(
                """{"elements":[{"type":"way","tags":{"highway":"primary"},
                    "geometry":[{"lat":999.0,"lon":0.0},{"lat":998.0,"lon":1.0}]}]}""",
            ).size,
        )
    }

    private fun buildings(): List<MapWay> =
        OverpassParser.parse(
            checkNotNull(javaClass.getResourceAsStream("/map/victoria-buildings.json")) {
                "missing test resource /map/victoria-buildings.json"
            }.bufferedReader().readText(),
        )

    private fun coastline(): List<MapWay> =
        OverpassParser.parse(
            checkNotNull(javaClass.getResourceAsStream("/map/victoria-coastline.json")) {
                "missing test resource /map/victoria-coastline.json"
            }.bufferedReader().readText(),
        )

    /**
     * The land/sea border, which the watch was missing entirely because the
     * query never asked for `natural=coastline` — a different tag from every
     * other water feature.
     */
    @Test
    fun coastlineParsesAndOutranksOrdinaryRoads() {
        val coast = coastline()
        assertTrue(coast.isNotEmpty(), "no coastline parsed from the fixture")
        assertTrue(
            coast.all { it.cls == MapClass.Coastline },
            "a non-coastline way came out of the coastline query",
        )
        // Cheap: a few long ways, not hundreds of short ones.
        assertTrue(coast.size < 20, "expected a handful of ways, got ${coast.size}")
        assertTrue(
            MapClass.Coastline.rank < MapClass.Residential.rank &&
                MapClass.Coastline.rank < MapClass.Service.rank,
            "the coastline must outrank ordinary streets — it is what says where the sea is",
        )
    }

    @Test
    fun coastlineSurvivesRenderingAtEveryRadius() {
        val all = ways() + coastline()
        for (radius in listOf(200.0, 800.0, 2000.0)) {
            val data = MapData(victoria, radius, all, 0L)
            val r = MapRenderer.render(data, victoria, radius)
            assertTrue(r.bytes.size <= 2048, "radius ${radius}m is ${r.bytes.size} B")
            assertEquals(
                SdpStatus.Ok,
                DisplayListInterpreter().render(r.bytes).status,
                "radius ${radius}m: parser rejected a list containing the coastline",
            )
        }
    }

    @Test
    fun buildingsParseAndRankBelowEveryRoad() {
        val outlines = buildings()
        assertTrue(outlines.size > 1000, "expected a dense town centre, got ${outlines.size}")
        assertTrue(
            outlines.all { it.cls == MapClass.Building },
            "a non-building way came out of the building query",
        )
        // The budget loop drops by rank, so this ordering is what guarantees a
        // road is never sacrificed for a shed.
        val worstRoad = MapClass.entries.filter { it != MapClass.Building }.maxOf { it.rank }
        assertTrue(
            MapClass.Building.rank > worstRoad,
            "buildings must rank below every road class",
        )
    }

    /**
     * At the radius where buildings are actually fetched, nearly all of them
     * survive the budget. This is the number `BUILDING_MAX_RADIUS_M` was set
     * from — if simplification or the cost model changes, the threshold needs
     * revisiting rather than silently going patchy.
     */
    @Test
    fun buildingsAreNearlyAllDrawnAtTheRadiusTheyAreFetchedAt() {
        val combined = ways() + buildings()
        val data = MapData(victoria, 150.0, combined, 0L)
        val r = MapRenderer.render(data, victoria, 150.0)
        assertTrue(r.bytes.size <= 2048, "150 m with buildings is ${r.bytes.size} B")
        assertEquals(SdpStatus.Ok, DisplayListInterpreter().render(r.bytes).status)
        val total = r.waysDrawn + r.waysDropped
        val droppedShare = r.waysDropped.toDouble() / total
        assertTrue(
            droppedShare < 0.10,
            "at 150 m, ${r.waysDropped}/$total dropped (${(droppedShare * 100).toInt()}%) — " +
                "BUILDING_MAX_RADIUS_M assumes under 10%",
        )
    }

    /**
     * Standing still must produce byte-identical output, because [MapAdapter]
     * suppresses a re-push on exactly that comparison. If rendering were not
     * deterministic the watch would take a full-screen repaint every location
     * interval for no change — straight into the open N-36 stall.
     */
    @Test
    fun renderingIsDeterministic() {
        val data = MapData(victoria, 400.0, ways(), 0L)
        val a = MapRenderer.render(data, victoria, 400.0)
        val b = MapRenderer.render(data, victoria, 400.0)
        assertTrue(a.bytes.contentEquals(b.bytes), "same input produced different bytes")
    }
}
