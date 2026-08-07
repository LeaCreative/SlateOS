package slate.app.map

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import slate.map.GeoPoint
import slate.map.MapData
import slate.map.OverpassParser
import slate.map.OverpassQuery
import slate.map.MapWay
import java.io.ByteArrayOutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max

/**
 * OpenStreetMap data for a small area, via the Overpass API.
 *
 * `HttpURLConnection` and `org.json`, matching [slate.app.repo.RepoHttp] — no
 * third-party HTTP stack, per the house rule that the actual calls stay visible.
 *
 * **This sends an approximate position to a third-party server.** That is
 * inherent in asking OSM what is nearby, but the request is deliberately blunt:
 * the bounding box is snapped to a fixed grid (see [snap]), so what leaves the
 * phone is a cell the user is somewhere inside, not their exact fix. The grid
 * doubles as the cache key, so standing still or walking within a cell produces
 * no further requests at all.
 *
 * Overpass is a free, donated, shared service. [MIN_QUERY_INTERVAL_MS] and the
 * identifying User-Agent are its usage policy, not decoration; a watch that
 * polled it per GPS fix would deserve the block it got.
 */
class OverpassClient(
    private val endpoint: String = DEFAULT_ENDPOINT,
    private val nowMs: () -> Long = { System.currentTimeMillis() },
) {
    private var lastQueryAtMs = 0L
    private var cacheKey: String? = null
    private var cached: MapData? = null

    /**
     * A fetch that should be tried again rather than reported as broken.
     *
     * [rateLimited] separates two things that were conflated and should not be:
     * a **429** is Overpass telling us to slow down, and deserves the full
     * backoff; a **504** is its gateway timing out under load, which is
     * transient, common, and fixed by asking again shortly. Treating a 504 as a
     * rate limit meant every one cost a 30 s stall on "Map is busy" — the delay
     * the operator reported.
     */
    class Busy(val retryInMs: Long, val rateLimited: Boolean = true) :
        Exception(
            if (rateLimited) "Overpass rate limit; retry in ${retryInMs}ms"
            else "Overpass busy (transient); retry in ${retryInMs}ms",
        )

    /**
     * Ways within [radiusM] of [around].
     *
     * Returns the cached result when the snapped cell has not changed, without
     * touching the network. Throws [Busy] when a fetch is wanted but the
     * minimum interval has not elapsed — the caller decides whether to keep
     * showing stale data or say so.
     */
    suspend fun fetch(around: GeoPoint, radiusM: Double): MapData {
        val key = cellKey(around, radiusM)
        cached?.let { if (key == cacheKey) return it }

        val since = nowMs() - lastQueryAtMs
        if (lastQueryAtMs != 0L && since < MIN_QUERY_INTERVAL_MS) {
            throw Busy(MIN_QUERY_INTERVAL_MS - since)
        }

        // Query around the snapped cell centre, not the raw fix, and widen by a
        // margin so panning within the cell does not walk off the data.
        val centre = snapCentre(around, radiusM)
        val queryRadius = radiusM * BBOX_MARGIN

        // Parse INSIDE the IO context, not just the fetch. Measured on the
        // desktop at ~50 ms for 190 KB of Overpass JSON, and this is called
        // from CompositorHost's scope, which is Dispatchers.Main.immediate —
        // so parsing outside would put a phone-scale multiple of that on the
        // main thread of the foreground service that owns the BLE link.
        val ways = withContext(Dispatchers.IO) {
            val json = get(
                OverpassQuery.build(
                    centre = centre,
                    fetchRadiusM = queryRadius,
                    // The VIEW radius, not the fetch radius. Passing the widened
                    // one here is what stopped buildings ever being requested.
                    viewRadiusM = radiusM,
                    timeoutS = QUERY_TIMEOUT_S,
                ),
            )
            parse(json)
        }
        lastQueryAtMs = nowMs()

        val data = MapData(
            centre = centre,
            radiusM = radiusM,
            ways = ways,
            fetchedAtMs = nowMs(),
        )
        cacheKey = key
        cached = data
        return data
    }

    /** Last result regardless of age, for drawing while a refetch is refused. */
    fun cachedOrNull(): MapData? = cached

    fun clearCache() {
        cached = null
        cacheKey = null
    }

    // ── Grid snapping ────────────────────────────────────────────────────────

    /**
     * Cell size in degrees for a given view radius.
     *
     * Roughly a third of the radius, so a user crossing a cell boundary still
     * has data covering the screen from the margin. Latitude only — longitude
     * uses the same metric size converted at this latitude, so cells stay
     * roughly square rather than stretching towards the poles.
     */
    private fun cellDegLat(radiusM: Double): Double =
        max(radiusM / 3.0, 50.0) / 111_132.0

    private fun cellDegLon(radiusM: Double, lat: Double): Double {
        val mPerDeg = 111_320.0 * cos(Math.toRadians(lat))
        return max(radiusM / 3.0, 50.0) / max(mPerDeg, 1.0)
    }

    private fun snap(v: Double, cell: Double): Double = floor(v / cell) * cell

    private fun snapCentre(p: GeoPoint, radiusM: Double): GeoPoint {
        val dLat = cellDegLat(radiusM)
        val dLon = cellDegLon(radiusM, p.lat)
        return GeoPoint(
            lat = snap(p.lat, dLat) + dLat / 2.0,
            lon = snap(p.lon, dLon) + dLon / 2.0,
        )
    }

    private fun cellKey(p: GeoPoint, radiusM: Double): String {
        val c = snapCentre(p, radiusM)
        return "%.6f,%.6f,%.0f".format(c.lat, c.lon, radiusM)
    }

    // ── Query ────────────────────────────────────────────────────────────────



    private suspend fun get(query: String): String = withContext(Dispatchers.IO) {
        val url = URL("$endpoint?data=" + URLEncoder.encode(query, "UTF-8"))
        if (url.protocol != "https") throw MapException("Overpass endpoint must be HTTPS")
        val conn = (url.openConnection() as HttpURLConnection).apply {
            connectTimeout = 15_000
            readTimeout = (QUERY_TIMEOUT_S + 10) * 1000
            requestMethod = "GET"
            instanceFollowRedirects = true
            // Overpass asks that clients identify themselves so an abusive one
            // can be blocked without blocking everyone behind the same IP.
            setRequestProperty("User-Agent", USER_AGENT)
            setRequestProperty("Accept", "application/json")
        }
        try {
            val code = conn.responseCode
            // 429 is "you are asking too often" — back off properly.
            if (code == 429) throw Busy(MIN_QUERY_INTERVAL_MS, rateLimited = true)
            // 504/503 are Overpass overloaded. Nothing we did caused it and
            // nothing we do fixes it except trying again soon; it also does not
            // count against our own query budget, since lastQueryAtMs is only
            // set on success.
            if (code == 504 || code == 503) {
                throw Busy(TRANSIENT_RETRY_MS, rateLimited = false)
            }
            if (code !in 200..299) throw MapException("Overpass HTTP $code")
            conn.inputStream.use { input ->
                val out = ByteArrayOutputStream()
                val buf = ByteArray(8192)
                var total = 0
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    total += n
                    if (total > MAX_RESPONSE_BYTES) {
                        throw MapException("Overpass response over ${MAX_RESPONSE_BYTES / 1024} KB")
                    }
                    out.write(buf, 0, n)
                }
                out.toByteArray().toString(Charsets.UTF_8)
            }
        } finally {
            conn.disconnect()
        }
    }

    // ── Parsing ─────────────────────────────────────────────────────────────

    /**
     * Delegated to [OverpassParser] in sdp-core so it can be tested on the
     * desktop against a captured response — see MapFixtureTest. This class
     * keeps only the parts that genuinely need Android and a network.
     */
    internal fun parse(json: String): List<MapWay> = OverpassParser.parse(json)

    companion object {
        /**
         * A public instance. Overpass runs on donated hardware; if this app
         * ever ships to more than a handful of watches it needs its own
         * endpoint or a commercial tile source, not more of someone else's
         * bandwidth.
         */
        const val DEFAULT_ENDPOINT = "https://overpass-api.de/api/interpreter"

        const val USER_AGENT = "Slate-Companion/0.6 (PineTime watch companion; OSM map sub-app)"

        /** Floor between network queries. Cache hits are not affected. */
        const val MIN_QUERY_INTERVAL_MS = 30_000L

        /**
         * Retry delay after Overpass returns 503/504.
         *
         * Short on purpose. A healthy overpass-api.de answers this query in
         * about 1.4 s, and its gateway timeouts are transient — two were hit
         * while capturing the test fixtures, and the very next attempt
         * succeeded. Waiting 30 s for one is a stall the user sees and nothing
         * gains from.
         */
        const val TRANSIENT_RETRY_MS = 4_000L

        const val QUERY_TIMEOUT_S = 25
        const val MAX_RESPONSE_BYTES = 2 * 1024 * 1024

        /** Fetch a wider area than is drawn, so movement inside a cell is covered. */
        const val BBOX_MARGIN = 1.6

    }
}

class MapException(message: String) : Exception(message)
