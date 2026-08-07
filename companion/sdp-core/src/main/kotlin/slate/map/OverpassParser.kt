package slate.map

import org.json.JSONObject

/**
 * Overpass JSON to [MapWay]s.
 *
 * Lives in sdp-core rather than beside the HTTP client so it can be tested on
 * the desktop against a real captured response. This parses third-party data
 * arriving over a network, which is the same argument `CLAUDE.md` makes for
 * BLE-facing parsers: the failure mode is a user in an unfamiliar place with a
 * blank watch, and that is not a good place to discover an edge case.
 *
 * Total by construction. Anything unrecognised is skipped, never thrown: one
 * malformed way must not cost the user the other two hundred.
 */
object OverpassParser {

    /** Expects `out geom`, which inlines each way's coordinates. */
    fun parse(json: String): List<MapWay> {
        val root = try {
            JSONObject(json)
        } catch (t: Throwable) {
            throw MapParseException("Overpass response was not JSON: ${t.message}")
        }
        val elements = root.optJSONArray("elements") ?: return emptyList()
        val ways = ArrayList<MapWay>(elements.length())
        for (i in 0 until elements.length()) {
            val e = elements.optJSONObject(i) ?: continue
            if (e.optString("type") != "way") continue
            val geometry = e.optJSONArray("geometry") ?: continue
            if (geometry.length() < 2) continue
            val tags = e.optJSONObject("tags")
            val cls = MapClass.fromTags(
                highway = tags?.optString("highway")?.ifBlank { null },
                waterway = tags?.optString("waterway")?.ifBlank { null },
                railway = tags?.optString("railway")?.ifBlank { null },
                building = tags?.optString("building")?.ifBlank { null },
                natural = tags?.optString("natural")?.ifBlank { null },
            ) ?: continue
            val points = ArrayList<GeoPoint>(geometry.length())
            for (j in 0 until geometry.length()) {
                val g = geometry.optJSONObject(j) ?: continue
                if (!g.has("lat") || !g.has("lon")) continue
                val lat = g.optDouble("lat")
                val lon = g.optDouble("lon")
                // Overpass has never returned a NaN here, but a coordinate that
                // is not a number would propagate all the way to a u8 cast and
                // land somewhere arbitrary on the screen.
                if (lat.isNaN() || lon.isNaN()) continue
                if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) continue
                points += GeoPoint(lat, lon)
            }
            if (points.size >= 2) ways += MapWay(cls, points)
        }
        return ways
    }
}

class MapParseException(message: String) : Exception(message)
