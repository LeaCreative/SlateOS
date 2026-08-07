package slate.map

/**
 * Vector map types, shared by the OSM fetcher, the projector and the renderer.
 *
 * No Android types: the whole pipeline from lat/lon to display-list bytes runs
 * on the desktop under test, which is the only way to know a screen fits its
 * budget without a phone and a watch in the loop.
 */

/** WGS84 degrees. */
data class GeoPoint(val lat: Double, val lon: Double)

/**
 * What a way is, coarsely, and how much it deserves the byte budget.
 *
 * [rank] orders emission when the budget runs out: lower goes on the screen
 * first. A rough map that shows the trunk road and drops a footpath is useful;
 * one that shows the footpath and drops the trunk road is not, and at 2 KB
 * something is always getting dropped.
 */
enum class MapClass(val rank: Int, val width: Int) {
    Motorway(0, 3),
    Trunk(1, 3),

    /**
     * The land/sea border — `natural=coastline`.
     *
     * Ranked above every road but the trunk network on purpose. On a coast it
     * is the single most useful line on the screen: it tells you which way the
     * sea is, which no amount of residential street does. It is also cheap,
     * being a handful of long ways rather than hundreds of short ones.
     *
     * OSM holds this as a *line*, not as filled water — the blue in a standard
     * OSM tile comes from separately generated polygons that are not in the
     * map data. Drawing the line is therefore both the cheap option and the
     * only one available from an Overpass query.
     */
    Coastline(2, 2),
    Primary(3, 2),
    Secondary(4, 2),
    Tertiary(5, 2),
    Residential(6, 1),
    Water(7, 2),
    Rail(8, 1),
    Service(9, 1),
    Path(10, 1),

    /**
     * Building outlines — **last**, and deliberately so.
     *
     * Measured around Victoria: 1575 buildings within 700 m, which want about
     * 28 KB of display list against a 2048 B budget the roads have already
     * spent most of. They are affordable only when the view is small enough
     * that there are few of them, which is why [OverpassClient] does not even
     * ask for them above a threshold radius — see BUILDING_MAX_RADIUS_M.
     *
     * Ranking them below every road means the existing budget loop needs no
     * special case: at a radius where they do not fit, they are simply the
     * first thing dropped.
     */
    Building(11, 1),
    ;

    companion object {
        /**
         * Map an OSM tag to a class. Unknown values become [Path] rather than
         * being dropped: an unrecognised road is still a line worth drawing if
         * budget remains, and OSM's tag vocabulary changes without warning.
         */
        fun fromTags(
            highway: String?,
            waterway: String?,
            railway: String?,
            building: String? = null,
            natural: String? = null,
        ): MapClass? {
            // Before everything else: a coastline way sometimes also carries
            // other tags, and it is the most important thing on the screen.
            if (natural == "coastline") return Coastline
            if (waterway != null) {
                return when (waterway) {
                    "river", "canal", "stream" -> Water
                    else -> null
                }
            }
            if (railway != null) {
                return when (railway) {
                    "rail", "light_rail", "subway", "tram" -> Rail
                    else -> null
                }
            }
            // Checked after the transport tags so a building with a highway
            // running through its footprint is still drawn as the road.
            if (highway == null && building != null && building != "no") {
                return Building
            }
            return when (highway) {
                null -> null
                "motorway", "motorway_link" -> Motorway
                "trunk", "trunk_link" -> Trunk
                "primary", "primary_link" -> Primary
                "secondary", "secondary_link" -> Secondary
                "tertiary", "tertiary_link", "unclassified" -> Tertiary
                "residential", "living_street" -> Residential
                "service" -> Service
                "footway", "path", "track", "cycleway", "pedestrian", "steps" -> Path
                else -> Path
            }
        }
    }
}

/** One OSM way, in geographic coordinates. */
data class MapWay(
    val cls: MapClass,
    val points: List<GeoPoint>,
)

/**
 * What the fetcher returns for one query.
 *
 * [centre] is the position the query was built around — **not** necessarily the
 * user's current position. The two drift apart between refetches, which is the
 * point: reprojecting cached ways around a moved user is free, and refetching
 * is not.
 */
data class MapData(
    val centre: GeoPoint,
    val radiusM: Double,
    val ways: List<MapWay>,
    val fetchedAtMs: Long,
)

/** A way projected into screen space, in device pixels. */
data class ScreenWay(
    val cls: MapClass,
    val points: List<ScreenPoint>,
)

data class ScreenPoint(val x: Int, val y: Int)
