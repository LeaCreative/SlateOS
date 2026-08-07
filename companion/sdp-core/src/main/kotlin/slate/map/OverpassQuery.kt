package slate.map

/**
 * Builds the Overpass QL for one map fetch.
 *
 * In sdp-core, not beside the HTTP client, because it is pure string logic and
 * belonged under test from the start. It was not, and the first thing it did
 * was hide a bug: the buildings threshold was compared against the **fetch**
 * radius instead of the **view** radius, so at the smallest radius the app
 * allows (100 m view, 160 m fetch) buildings were still never requested. There
 * was no setting at which the feature could work, and nothing caught it because
 * the renderer tests hand `MapRenderer` data that already contains buildings.
 *
 * Hence the two radii are separate, named parameters here. Confusing them is
 * the mistake this file exists to make impossible.
 */
object OverpassQuery {

    /**
     * Above this **view** radius, building outlines are not requested.
     *
     * Set from the measured share the display-list budget has to discard,
     * rendering the Victoria fixture at each radius:
     *
     * | view radius | buildings | dropped |
     * |-------------|-----------|---------|
     * | 100 m       | 53        | 0%      |
     * | 150 m       | 142       | 3.5%    |
     * | 175 m       | 190       | 29%     |
     * | 250 m       | 375       | 60%     |
     *
     * The knee is between 150 and 175. Past it a map reads as arbitrary rather
     * than sparse — a dropped building looks like open ground. Buildings are
     * also six times the data of every road, waterway and railway combined
     * (181 KB gzipped against 31 KB at 700 m), so asking for them at a radius
     * where most would be discarded costs the user data for nothing.
     */
    const val BUILDING_MAX_VIEW_RADIUS_M = 150.0

    fun wantsBuildings(viewRadiusM: Double): Boolean =
        viewRadiusM <= BUILDING_MAX_VIEW_RADIUS_M

    /**
     * @param centre         snapped cell centre the query is built around
     * @param fetchRadiusM   how far to ask for — wider than the view, so
     *                       movement inside the cell stays covered
     * @param viewRadiusM    what will actually be drawn; the buildings
     *                       threshold is judged on **this**
     */
    fun build(
        centre: GeoPoint,
        fetchRadiusM: Double,
        viewRadiusM: Double,
        timeoutS: Int,
    ): String {
        val r = fetchRadiusM.toInt()
        val lat = "%.6f".format(centre.lat)
        val lon = "%.6f".format(centre.lon)
        val around = "around:$r,$lat,$lon"
        // `way` only: no relations, no metadata. The response is the dominant
        // cost on a phone link and most of a full Overpass response is data
        // this cannot draw.
        val clauses = buildList {
            add("""way($around)["highway"];""")
            add("""way($around)["waterway"~"river|canal|stream"];""")
            add("""way($around)["railway"~"rail|light_rail|subway|tram"];""")
            // Requested at every radius, unlike buildings. A coastline is a
            // few long ways, so it costs almost nothing, and it is the line
            // that orients the whole screen on a coast.
            add("""way($around)["natural"="coastline"];""")
            if (wantsBuildings(viewRadiusM)) add("""way($around)["building"];""")
        }
        return buildString {
            append("[out:json][timeout:").append(timeoutS).append("];\n")
            append("(\n")
            for (c in clauses) append("  ").append(c).append('\n')
            append(");\n")
            append("out geom;")
        }
    }
}
