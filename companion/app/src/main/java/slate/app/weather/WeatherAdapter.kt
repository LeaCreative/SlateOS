package slate.app.weather

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Handler
import android.os.Looper
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.weather.WeatherSnapshot
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume

/**
 * Host weather via Open-Meteo forecast + marine sea level. JS only sees
 * [WeatherSnapshot] JSON. Coords always come from the phone GPS/network fix
 * (explicit lat/lon from the script still wins when provided).
 */
class WeatherAdapter(
    private val context: Context,
    private val scope: CoroutineScope,
    private val onEvent: (json: String) -> Unit,
) {
    private var job: Job? = null

    fun fetch(lat: Double?, lon: Double?) {
        job?.cancel()
        emit(JSONObject().put("type", "status").put("state", "loading"))
        job = scope.launch {
            try {
                val coords = resolveCoords(lat, lon)
                    ?: run {
                        emit(
                            JSONObject()
                                .put("type", "status")
                                .put("state", "error")
                                .put("detail", "no location"),
                        )
                        return@launch
                    }
                val (la, lo) = coords
                val forecastUrl = String.format(
                    Locale.US,
                    "https://api.open-meteo.com/v1/forecast" +
                        "?latitude=%.4f&longitude=%.4f" +
                        "&current=temperature_2m,weather_code,precipitation,wind_speed_10m" +
                        "&wind_speed_unit=ms&timezone=auto",
                    la,
                    lo,
                )
                val marineUrl = String.format(
                    Locale.US,
                    "https://marine-api.open-meteo.com/v1/marine" +
                        "?latitude=%.4f&longitude=%.4f" +
                        "&hourly=sea_level_height_msl" +
                        "&forecast_days=3&timezone=auto",
                    la,
                    lo,
                )
                val forecastDef = async(Dispatchers.IO) {
                    runCatching { httpGet(forecastUrl) }
                }
                val marineDef = async(Dispatchers.IO) {
                    runCatching { httpGet(marineUrl) }
                }
                val forecast = forecastDef.await().getOrElse { throw it }
                val marine = marineDef.await().getOrNull()
                if (marine == null) {
                    LinkLog.i("weather.marine unavailable (inland or network)")
                }
                val snap = WeatherSnapshot.parseOpenMeteo(forecast, la, lo, marine)
                onEvent(snap.toJson().toString())
            } catch (t: Throwable) {
                LinkLog.w("weather.fetch failed: ${t.message}")
                emit(
                    JSONObject()
                        .put("type", "status")
                        .put("state", "error")
                        .put("detail", (t.message ?: "network").take(80)),
                )
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private fun emit(o: JSONObject) {
        onEvent(o.toString())
    }

    private suspend fun resolveCoords(lat: Double?, lon: Double?): Pair<Double, Double>? {
        if (lat != null && lon != null &&
            lat.isFinite() && lon.isFinite() &&
            lat in -90.0..90.0 && lon in -180.0..180.0
        ) {
            return lat to lon
        }
        if (!hasLocationPermission()) return lastKnownCoords()
        val fresh = withTimeoutOrNull(LOC_WAIT_MS) { awaitFreshFix() }
        if (fresh != null) return fresh.latitude to fresh.longitude
        return lastKnownCoords()
    }

    private fun hasLocationPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_COARSE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED ||
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    @Suppress("MissingPermission")
    private fun lastKnownCoords(): Pair<Double, Double>? {
        val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
            ?: return null
        val providers = listOf(
            LocationManager.GPS_PROVIDER,
            LocationManager.NETWORK_PROVIDER,
            LocationManager.PASSIVE_PROVIDER,
        )
        var best: Location? = null
        for (p in providers) {
            try {
                val loc = lm.getLastKnownLocation(p) ?: continue
                if (best == null || loc.time > best!!.time) best = loc
            } catch (_: SecurityException) {
                return null
            } catch (_: Throwable) {
                // try next
            }
        }
        val loc = best ?: return null
        return loc.latitude to loc.longitude
    }

    @Suppress("MissingPermission")
    private suspend fun awaitFreshFix(): Location? = suspendCancellableCoroutine { cont ->
        val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
        if (lm == null) {
            cont.resume(null)
            return@suspendCancellableCoroutine
        }
        val cached = lastKnownLocation(lm)
        if (cached != null && System.currentTimeMillis() - cached.time <= FRESH_MS) {
            cont.resume(cached)
            return@suspendCancellableCoroutine
        }
        val done = AtomicBoolean(false)
        val main = Handler(Looper.getMainLooper())
        lateinit var listener: LocationListener
        fun finish(loc: Location?) {
            if (!done.compareAndSet(false, true)) return
            runCatching { lm.removeUpdates(listener) }
            if (cont.isActive) cont.resume(loc)
        }
        listener = LocationListener { loc -> finish(loc) }
        cont.invokeOnCancellation {
            done.set(true)
            runCatching { lm.removeUpdates(listener) }
        }
        main.post {
            val providers = listOf(
                LocationManager.GPS_PROVIDER,
                LocationManager.NETWORK_PROVIDER,
            ).filter { runCatching { lm.isProviderEnabled(it) }.getOrDefault(false) }
            if (providers.isEmpty()) {
                finish(cached)
                return@post
            }
            for (p in providers) {
                try {
                    lm.requestLocationUpdates(p, 0L, 0f, listener, Looper.getMainLooper())
                } catch (_: Throwable) {
                    // try next
                }
            }
        }
    }

    @Suppress("MissingPermission")
    private fun lastKnownLocation(lm: LocationManager): Location? {
        var best: Location? = null
        for (p in listOf(
            LocationManager.GPS_PROVIDER,
            LocationManager.NETWORK_PROVIDER,
            LocationManager.PASSIVE_PROVIDER,
        )) {
            try {
                val loc = lm.getLastKnownLocation(p) ?: continue
                if (best == null || loc.time > best!!.time) best = loc
            } catch (_: Throwable) {
                // skip
            }
        }
        return best
    }

    companion object {
        private const val LOC_WAIT_MS = 8_000L
        private const val FRESH_MS = 5 * 60_000L

        private fun httpGet(url: String): String {
            val conn = (URL(url).openConnection() as HttpURLConnection).apply {
                connectTimeout = 15_000
                readTimeout = 20_000
                instanceFollowRedirects = true
                requestMethod = "GET"
                setRequestProperty("User-Agent", "SlateWeather/1.0 (PineTime companion)")
                setRequestProperty("Accept", "application/json")
            }
            try {
                val code = conn.responseCode
                val stream = if (code in 200..299) conn.inputStream else conn.errorStream
                    ?: error("HTTP $code")
                if (code !in 200..299) error("HTTP $code")
                return stream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            } finally {
                conn.disconnect()
            }
        }
    }
}
