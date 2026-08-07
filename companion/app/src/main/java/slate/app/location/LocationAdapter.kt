package slate.app.location

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import androidx.core.content.ContextCompat
import org.json.JSONObject

/**
 * The phone's position, for sub-apps holding the `location` permission.
 *
 * Raw `android.location.LocationManager`, not Play Services' fused provider —
 * same reason `CLAUDE.md` gives for using raw `android.bluetooth`: the actual
 * calls stay visible, and the build keeps working offline with no Google
 * dependency. The cost is that provider selection is ours to do.
 *
 * Every path reports something. A sub-app that asked for a fix and hears
 * nothing cannot tell "permission refused" from "still searching" from "this
 * phone has no GPS", and all three are ordinary. That distinction is the whole
 * reason [Status] exists.
 */
class LocationAdapter(
    private val context: Context,
    private val listener: (json: String) -> Unit,
) {
    enum class Status(val wire: String) {
        /** Listening; no fix has arrived yet. */
        Searching("searching"),

        /** The companion lacks the Android runtime permission. Terminal. */
        Denied("denied"),

        /** Location is off phone-wide. Terminal until the user changes it. */
        Disabled("disabled"),

        /** No usable provider on this device. Terminal. */
        Unavailable("unavailable"),
    }

    private val manager: LocationManager? =
        context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager

    private var listening = false
    private var oneShot = false

    private val callback = LocationListener { loc -> deliver(loc) }

    fun hasPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_COARSE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED ||
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    /**
     * Start a stream of fixes.
     *
     * [minIntervalMs] is floored at [MIN_INTERVAL_MS] here as well as in the JS
     * binding. The binding is a convenience; this is the one that counts, since
     * a sub-app's script is untrusted input and could emit the adapter command
     * directly.
     */
    fun subscribe(minIntervalMs: Long, minDistanceM: Float): Status {
        val gate = precondition()
        if (gate != null) {
            emitStatus(gate)
            return gate
        }
        stop()
        val interval = minIntervalMs.coerceAtLeast(MIN_INTERVAL_MS)
        val distance = minDistanceM.coerceIn(0f, MAX_MIN_DISTANCE_M)
        val providers = usableProviders()
        if (providers.isEmpty()) {
            emitStatus(Status.Unavailable)
            return Status.Unavailable
        }
        for (provider in providers) {
            try {
                @Suppress("MissingPermission")
                manager?.requestLocationUpdates(provider, interval, distance, callback)
            } catch (t: Throwable) {
                // A provider can disappear between the query and the request.
                // Keep going: another may still work.
                continue
            }
        }
        listening = true
        // Answer immediately from the cache when it is fresh, so a screen has
        // something to draw before the first real fix lands.
        val cached = lastKnown()
        if (cached != null) deliver(cached) else emitStatus(Status.Searching)
        return Status.Searching
    }

    /** One fix, then stop. */
    fun requestSingle(): Status {
        val gate = precondition()
        if (gate != null) {
            emitStatus(gate)
            return gate
        }
        val cached = lastKnown()
        if (cached != null && isFresh(cached)) {
            deliver(cached)
            return Status.Searching
        }
        oneShot = true
        return subscribe(MIN_INTERVAL_MS, 0f)
    }

    fun stop() {
        if (!listening) return
        runCatching { manager?.removeUpdates(callback) }
        listening = false
        oneShot = false
    }

    /** Null when a subscription may proceed; otherwise why it may not. */
    private fun precondition(): Status? {
        val m = manager ?: return Status.Unavailable
        if (!hasPermission()) return Status.Denied
        val on = runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                m.isLocationEnabled
            } else {
                @Suppress("DEPRECATION")
                m.isProviderEnabled(LocationManager.GPS_PROVIDER) ||
                    m.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
            }
        }.getOrDefault(false)
        if (!on) return Status.Disabled
        return null
    }

    private fun usableProviders(): List<String> {
        val m = manager ?: return emptyList()
        val wanted = listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)
        return wanted.filter { p ->
            runCatching { m.isProviderEnabled(p) }.getOrDefault(false)
        }
    }

    private fun lastKnown(): Location? {
        val m = manager ?: return null
        if (!hasPermission()) return null
        var best: Location? = null
        for (p in usableProviders()) {
            val loc = try {
                @Suppress("MissingPermission")
                m.getLastKnownLocation(p)
            } catch (_: Throwable) {
                null
            } ?: continue
            if (best == null || loc.time > best.time) best = loc
        }
        return best
    }

    private fun isFresh(loc: Location): Boolean =
        System.currentTimeMillis() - loc.time <= CACHE_MAX_AGE_MS

    private fun deliver(loc: Location) {
        listener(toJson(loc))
        if (oneShot) stop()
    }

    private fun emitStatus(s: Status) {
        listener(
            JSONObject()
                .put("type", "status")
                .put("state", s.wire)
                .toString(),
        )
    }

    companion object {
        /**
         * Floor on how often a sub-app can be given a fix. A script that asked
         * for 50 ms updates would hold the GPS on and flatten the phone, and it
         * would do so on behalf of a downloaded script.
         */
        const val MIN_INTERVAL_MS = 1000L
        const val MAX_MIN_DISTANCE_M = 10_000f

        /** A cached fix older than this is stale enough to keep searching. */
        const val CACHE_MAX_AGE_MS = 120_000L

        /**
         * Coordinates are rounded before they leave the phone.
         *
         * Six decimal places is ~0.1 m — far finer than any consumer GPS, and
         * finer than anything a 240x240 watch face can show. Rounding keeps a
         * sub-app from being handed more precision than it has any use for.
         */
        private const val COORD_DECIMALS = 6

        fun toJson(loc: Location): String {
            val o = JSONObject()
                .put("type", "fix")
                .put("lat", round(loc.latitude))
                .put("lon", round(loc.longitude))
                .put("timeEpochSec", loc.time / 1000L)
                .put("provider", loc.provider ?: "")
            if (loc.hasAccuracy()) o.put("accuracyM", loc.accuracy.toDouble())
            if (loc.hasAltitude()) o.put("altitudeM", loc.altitude)
            if (loc.hasSpeed()) o.put("speedMps", loc.speed.toDouble())
            if (loc.hasBearing()) o.put("bearingDeg", loc.bearing.toDouble())
            return o.toString()
        }

        private fun round(v: Double): Double {
            var factor = 1.0
            repeat(COORD_DECIMALS) { factor *= 10.0 }
            return Math.round(v * factor) / factor
        }
    }
}
