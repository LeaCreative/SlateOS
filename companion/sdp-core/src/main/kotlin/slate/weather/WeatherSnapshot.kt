package slate.weather

import org.json.JSONObject

/**
 * Small typed weather snapshot for JS sub-apps. Parsed from Open-Meteo
 * `current` JSON — no Android types so desktop tests can cover it.
 */
data class WeatherSnapshot(
    val tempC: Double,
    val weatherCode: Int,
    val label: String,
    val precipMm: Double,
    val windMps: Double,
    val lat: Double,
    val lon: Double,
) {
    fun toJson(): JSONObject = JSONObject()
        .put("type", "snapshot")
        .put("tempC", tempC)
        .put("weatherCode", weatherCode)
        .put("label", label)
        .put("precipMm", precipMm)
        .put("windMps", windMps)
        .put("lat", lat)
        .put("lon", lon)

    companion object {
        fun parseOpenMeteo(body: String, lat: Double, lon: Double): WeatherSnapshot {
            val root = JSONObject(body)
            val cur = root.optJSONObject("current")
                ?: error("no current block")
            val code = cur.optInt("weather_code", -1)
            return WeatherSnapshot(
                tempC = cur.optDouble("temperature_2m", Double.NaN),
                weatherCode = code,
                label = labelFor(code),
                precipMm = cur.optDouble("precipitation", 0.0),
                windMps = cur.optDouble("wind_speed_10m", 0.0),
                lat = lat,
                lon = lon,
            )
        }

        /** WMO weather interpretation codes → short watch label. */
        fun labelFor(code: Int): String = when (code) {
            0 -> "Clear"
            1, 2 -> "Fair"
            3 -> "Cloudy"
            45, 48 -> "Fog"
            in 51..57 -> "Drizzle"
            in 61..67 -> "Rain"
            in 71..77 -> "Snow"
            in 80..82 -> "Showers"
            in 85..86 -> "Snow shwr"
            in 95..99 -> "Thunder"
            else -> "Code $code"
        }
    }
}
