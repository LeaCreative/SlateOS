package slate.map

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * The Overpass query text.
 *
 * This existed untested inside the app module, and the gap cost the buildings
 * feature entirely: the threshold was compared against the **fetch** radius
 * (view x 1.6) instead of the view radius, so the widest view that could ever
 * have requested buildings was 93.75 m — below the 100 m minimum the manifest
 * allows. The feature shipped in a state where no setting could turn it on, and
 * every existing test passed, because they hand `MapRenderer` data that already
 * contains buildings and never build a query at all.
 */
class OverpassQueryTest {

    private val victoria = GeoPoint(-4.6191, 55.4513)

    private fun query(viewRadiusM: Double) = OverpassQuery.build(
        centre = victoria,
        fetchRadiusM = viewRadiusM * 1.6,
        viewRadiusM = viewRadiusM,
        timeoutS = 25,
    )

    /**
     * The regression, stated at the setting the user would actually pick.
     * 140 m is inside the documented threshold and must ask for buildings even
     * though its fetch radius (224 m) is not.
     */
    @Test
    fun buildingsAreRequestedAtEveryRadiusTheThresholdAllows() {
        for (viewRadius in listOf(100.0, 120.0, 140.0, 150.0)) {
            assertTrue(
                query(viewRadius).contains("\"building\""),
                "no building clause at a ${viewRadius}m view — the threshold is " +
                    "${OverpassQuery.BUILDING_MAX_VIEW_RADIUS_M}m, so this must ask for them",
            )
        }
    }

    @Test
    fun buildingsAreNotRequestedAboveTheThreshold() {
        for (viewRadius in listOf(151.0, 200.0, 400.0, 2000.0)) {
            assertFalse(
                query(viewRadius).contains("\"building\""),
                "a ${viewRadius}m view asked for buildings; the response is six " +
                    "times larger and most would be dropped",
            )
        }
    }

    /**
     * The two radii must not be confused again. The query asks Overpass for the
     * **fetch** radius — wider than the view, so movement inside the cached
     * cell stays covered — while the buildings decision uses the view.
     */
    @Test
    fun theQueryAsksForTheFetchRadiusNotTheViewRadius() {
        val q = OverpassQuery.build(
            centre = victoria,
            fetchRadiusM = 640.0,
            viewRadiusM = 400.0,
            timeoutS = 25,
        )
        assertTrue(q.contains("around:640,"), "expected the fetch radius in the query")
        assertFalse(q.contains("around:400,"), "the view radius must not be queried")
    }

    @Test
    fun everyQueryIsWellFormedOverpassQl() {
        val q = query(140.0)
        assertTrue(q.startsWith("[out:json][timeout:25];"), "missing settings line:\n$q")
        assertTrue(q.trimEnd().endsWith("out geom;"), "missing output statement:\n$q")
        // Balanced union braces — the ones on their own line. Counting every
        // parenthesis in the string would also count `way(around:...)`, which
        // is how the first version of this assertion managed to fail on a
        // perfectly valid query.
        val lines = q.lines().map { it.trim() }
        assertEquals(1, lines.count { it == "(" }, "expected one union opener:\n$q")
        assertEquals(1, lines.count { it == ");" }, "expected one union closer:\n$q")
        for (line in q.lines().map { it.trim() }.filter { it.startsWith("way(") }) {
            assertTrue(line.endsWith(";"), "unterminated clause: $line")
        }
        // Coordinates are formatted, not raw doubles — Overpass rejects
        // scientific notation and this is where a locale could bite too.
        assertTrue(q.contains("-4.619100"), "latitude not formatted to 6 dp:\n$q")
        assertTrue(q.contains("55.451300"), "longitude not formatted to 6 dp:\n$q")
    }

    @Test
    fun roadsWaterAndRailAreAlwaysRequested() {
        val q = query(2000.0)
        assertTrue(q.contains("\"highway\""))
        assertTrue(q.contains("waterway"))
        assertTrue(q.contains("railway"))
    }

    /**
     * The land/sea border is asked for at **every** radius, unlike buildings.
     * It is a handful of long ways rather than hundreds of small ones — three
     * ways and 16 KB around Victoria — and on a coast it is the line that
     * orients the entire screen.
     */
    @Test
    fun coastlineIsRequestedAtEveryRadius() {
        for (viewRadius in listOf(100.0, 140.0, 400.0, 2000.0)) {
            assertTrue(
                query(viewRadius).contains("\"natural\"=\"coastline\""),
                "no coastline clause at a ${viewRadius}m view",
            )
        }
    }

    /**
     * The threshold constant and the radius range the manifest offers have to
     * overlap, or the feature is unreachable by construction — which is exactly
     * what happened. `examples/map/manifest.json` declares min 100.
     */
    @Test
    fun theThresholdIsReachableFromTheDeclaredSettingRange() {
        val manifestMinRadius = 100.0
        assertTrue(
            OverpassQuery.wantsBuildings(manifestMinRadius),
            "the smallest radius the settings screen allows (${manifestMinRadius}m) " +
                "cannot request buildings — the feature would be impossible to enable",
        )
    }
}
