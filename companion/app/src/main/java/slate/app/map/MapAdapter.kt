package slate.app.map

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.app.location.LocationAdapter
import slate.map.GeoPoint
import slate.map.MapData
import slate.map.MapRenderer

/**
 * Drives the map: position in, display list out.
 *
 * **The companion refreshes the map; the sub-app never asks it to.** The script
 * subscribes once and then only receives — there is no refresh command in the
 * binding, so there is nothing for a sub-app to poll with and no way for a
 * badly written one to hammer Overpass. The location stream is the clock:
 * every fix is a chance to redraw, and this class decides what that costs.
 *
 * Two tiers, because they have very different prices:
 *
 * - **Reproject** — free, local, runs on every fix. Cached ways are redrawn
 *   around the new position, so the map tracks the user continuously.
 * - **Refetch** — network, rate-limited, runs only when the user has left the
 *   area the cached data covers or that data has gone stale.
 *
 * Without that split the map either lurches between fetches or hammers a free
 * public API. With it, walking across a screen is smooth and costs nothing.
 */
class MapAdapter(
    private val context: Context,
    private val scope: CoroutineScope,
    private val client: OverpassClient = OverpassClient(),
    private val onDisplayList: (bytes: ByteArray) -> Unit,
    private val onStatus: (json: String) -> Unit,
    private val nowMs: () -> Long = { System.currentTimeMillis() },
) {
    private var location: LocationAdapter? = null
    private var radiusM: Double = DEFAULT_RADIUS_M
    private var viewer: GeoPoint? = null
    private var data: MapData? = null
    private var fetchedAround: GeoPoint? = null
    private var fetchJob: Job? = null
    private var retryJob: Job? = null
    private var lastPushed: ByteArray? = null
    private var started = false

    fun start(radiusM: Double) {
        this.radiusM = radiusM.coerceIn(MIN_RADIUS_M, MAX_RADIUS_M)
        if (started) return
        started = true
        lastPushed = null
        val adapter = LocationAdapter(context) { json -> onLocationJson(json) }
        location = adapter
        val status = adapter.subscribe(LOCATION_INTERVAL_MS, LOCATION_MIN_DISTANCE_M)
        if (status != LocationAdapter.Status.Searching) {
            // Terminal: no fix is coming. Say which, and stop pretending.
            emit("blocked", detail = status.wire)
            stop()
        } else {
            emit("locating")
        }
    }

    fun stop() {
        started = false
        fetchJob?.cancel()
        fetchJob = null
        retryJob?.cancel()
        retryJob = null
        location?.stop()
        location = null
        lastPushed = null
    }

    /** Current view radius in metres, for the status line. */
    fun radius(): Double = radiusM

    // ── Location in ──────────────────────────────────────────────────────────

    private fun onLocationJson(json: String) {
        val o = try {
            JSONObject(json)
        } catch (_: Throwable) {
            return
        }
        when (o.optString("type")) {
            "status" -> {
                val state = o.optString("state")
                if (state != "searching") emit("blocked", detail = state)
            }
            "fix" -> {
                if (!o.has("lat") || !o.has("lon")) return
                onFix(GeoPoint(o.optDouble("lat"), o.optDouble("lon")))
            }
        }
    }

    private fun onFix(p: GeoPoint) {
        if (!started) return
        viewer = p
        if (needsRefetch(p)) refetch(p)
        // Draw with whatever is already cached, including nothing. Waiting for
        // the network before showing anything would leave the watch blank for
        // seconds every time the user crosses a cell boundary.
        redraw()
    }

    // ── Refresh policy ───────────────────────────────────────────────────────

    private fun needsRefetch(p: GeoPoint): Boolean {
        if (fetchJob?.isActive == true) return false
        val current = data ?: return true
        val origin = fetchedAround ?: return true
        val moved = slate.map.MapProjection(origin, radiusM).distanceM(origin, p)
        if (moved > radiusM * REFETCH_DISTANCE_FRACTION) return true
        if (nowMs() - current.fetchedAtMs > MAX_DATA_AGE_MS) return true
        return false
    }

    private fun refetch(p: GeoPoint) {
        fetchJob = scope.launch {
            try {
                if (data == null) emit("loading")
                val fresh = client.fetch(p, radiusM)
                data = fresh
                fetchedAround = p
                LinkLog.i("map: fetched ${fresh.ways.size} ways around ${fmt(p)} r=${radiusM.toInt()}m")
                redraw()
            } catch (b: OverpassClient.Busy) {
                // Rate-limited, or Overpass returned 504/429. Keep drawing the
                // stale map rather than blanking it — an old map is far more
                // useful than no map, and the user is told it is old.
                LinkLog.i("map: refetch deferred ${b.retryInMs}ms")
                if (data == null) emit("waiting")
                scheduleRetry(b.retryInMs)
            } catch (t: Throwable) {
                LinkLog.e("map: fetch failed", t)
                if (data == null) emit("error", detail = t.message ?: "fetch failed")
                scheduleRetry(RETRY_AFTER_ERROR_MS)
            }
        }
    }

    /**
     * Try again after a failure, on our own clock.
     *
     * The location stream is the map's clock for everything else, and that is
     * fine while it ticks — but it legitimately goes quiet when the user is not
     * moving, and a failed fetch then has nothing to retry it. Observed exactly
     * that: Overpass returned a 504, the user was sitting still, and the watch
     * showed "Map is busy" indefinitely because no further fix ever arrived to
     * drive another attempt.
     */
    private fun scheduleRetry(delayMs: Long) {
        if (!started) return
        retryJob?.cancel()
        retryJob = scope.launch {
            delay(delayMs.coerceIn(MIN_RETRY_MS, MAX_RETRY_MS))
            val p = viewer
            if (started && data == null && p != null) {
                LinkLog.i("map: retrying fetch")
                refetch(p)
            }
        }
    }

    // ── Render out ───────────────────────────────────────────────────────────

    private fun redraw() {
        val p = viewer ?: return
        val d = data ?: return
        val result = MapRenderer.render(
            data = d,
            viewer = p,
            radiusM = radiusM,
            budgetBytes = BUDGET_BYTES,
        )
        // Standing still produces an identical list every interval. Pushing it
        // again would spend a BLE window and a full-screen repaint to change
        // nothing — and repaints are the open N-36 stall.
        val previous = lastPushed
        if (previous != null && previous.contentEquals(result.bytes)) return
        lastPushed = result.bytes
        onDisplayList(result.bytes)
        emit(
            "ok",
            extra = JSONObject()
                .put("ways", result.waysDrawn)
                .put("dropped", result.waysDropped)
                .put("bytes", result.bytes.size)
                .put("scaleM", result.scaleBarMetres)
                .put("ageSec", (nowMs() - d.fetchedAtMs) / 1000L),
        )
        LinkLog.i(
            "map: pushed ${result.bytes.size} B, ${result.waysDrawn} ways" +
                if (result.waysDropped > 0) " (${result.waysDropped} dropped for budget)" else "",
        )
    }

    private fun emit(state: String, detail: String? = null, extra: JSONObject? = null) {
        val o = extra ?: JSONObject()
        o.put("type", "status").put("state", state)
        if (detail != null) o.put("detail", detail)
        onStatus(o.toString())
    }

    private fun fmt(p: GeoPoint) = "%.4f,%.4f".format(p.lat, p.lon)

    companion object {
        const val DEFAULT_RADIUS_M = 400.0
        const val MIN_RADIUS_M = 100.0
        const val MAX_RADIUS_M = 2000.0

        /**
         * How often a position is wanted. Also the map's frame rate: there is
         * no separate timer, so nothing redraws when no fix arrives.
         */
        const val LOCATION_INTERVAL_MS = 5_000L

        /**
         * No distance filter, deliberately.
         *
         * It was 5 m, which sounds like a sensible way to avoid redundant work
         * and is not: a stationary user then receives exactly one fix — the
         * cached one delivered at subscribe — and never another. The map's
         * whole clock is the location stream, so that silence also stopped
         * failed fetches from ever being retried. [LOCATION_INTERVAL_MS] is the
         * throttle; redundant redraws are already suppressed by comparing the
         * rendered bytes against the last push, which costs nothing.
         */
        const val LOCATION_MIN_DISTANCE_M = 0f

        /** A fetch that failed outright, rather than being deferred. */
        const val RETRY_AFTER_ERROR_MS = 15_000L
        /**
         * Floor on a retry delay. Low enough that a transient Overpass 504
         * costs a pause rather than a stall — the client asks for ~4 s and this
         * must not silently stretch it back out.
         */
        const val MIN_RETRY_MS = 3_000L
        const val MAX_RETRY_MS = 60_000L

        /** Refetch after moving this fraction of the view radius. */
        const val REFETCH_DISTANCE_FRACTION = 0.4

        /** Refetch data older than this even when standing still. */
        const val MAX_DATA_AGE_MS = 10 * 60 * 1000L

        /** docs/subapp-rules.md §2 practical limit. */
        const val BUDGET_BYTES = 2048
    }
}
