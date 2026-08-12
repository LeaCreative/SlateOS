package slate.app.weather

import android.content.Context
import android.location.LocationManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.weather.WeatherSnapshot
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale

/**
 * Host weather via Open-Meteo. JS only sees [WeatherSnapshot] JSON.
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
                val url = String.format(
                    Locale.US,
                    "https://api.open-meteo.com/v1/forecast" +
                        "?latitude=%.4f&longitude=%.4f" +
                        "&current=temperature_2m,weather_code,precipitation,wind_speed_10m" +
                        "&wind_speed_unit=ms&timezone=auto",
                    la,
                    lo,
                )
                val body = withContext(Dispatchers.IO) { httpGet(url) }
                val snap = WeatherSnapshot.parseOpenMeteo(body, la, lo)
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

    @Suppress("MissingPermission")
    private fun resolveCoords(lat: Double?, lon: Double?): Pair<Double, Double>? {
        if (lat != null && lon != null &&
            lat.isFinite() && lon.isFinite() &&
            lat in -90.0..90.0 && lon in -180.0..180.0
        ) {
            return lat to lon
        }
        val lm = context.getSystemService(Context.LOCATION_SERVICE) as? LocationManager
            ?: return null
        val providers = listOf(
            LocationManager.GPS_PROVIDER,
            LocationManager.NETWORK_PROVIDER,
            LocationManager.PASSIVE_PROVIDER,
        )
        var best: android.location.Location? = null
        for (p in providers) {
            try {
                val loc = lm.getLastKnownLocation(p) ?: continue
                if (best == null || loc.time > best!!.time) best = loc
            } catch (_: SecurityException) {
                return null
            } catch (_: Throwable) {
                // try next provider
            }
        }
        val loc = best ?: return null
        return loc.latitude to loc.longitude
    }

    companion object {
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
