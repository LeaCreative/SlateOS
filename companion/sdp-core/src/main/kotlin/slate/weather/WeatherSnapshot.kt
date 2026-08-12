package slate.weather

import org.json.JSONObject
import java.time.Instant
import java.time.LocalDateTime
import java.time.ZoneId
import java.util.Locale

/**
 * Small typed weather snapshot for JS sub-apps. Parsed from Open-Meteo
 * forecast + optional marine `sea_level_height_msl` — no Android types so
 * desktop tests can cover it.
 */
data class WeatherSnapshot(
    val tempC: Double,
    val weatherCode: Int,
    val label: String,
    val precipMm: Double,
    val windMps: Double,
    val lat: Double,
    val lon: Double,
    /** Metres above global mean sea level; null if marine data unavailable. */
    val seaLevelM: Double? = null,
    /** Local clock "HH:MM" of the next high tide, or null. */
    val nextHighTime: String? = null,
    val nextHighM: Double? = null,
    /** Local clock "HH:MM" of the next low tide, or null. */
    val nextLowTime: String? = null,
    val nextLowM: Double? = null,
) {
    fun toJson(): JSONObject {
        val o = JSONObject()
            .put("type", "snapshot")
            .put("tempC", tempC)
            .put("weatherCode", weatherCode)
            .put("label", label)
            .put("precipMm", precipMm)
            .put("windMps", windMps)
            .put("lat", lat)
            .put("lon", lon)
        if (seaLevelM != null && seaLevelM.isFinite()) {
            o.put("seaLevelM", seaLevelM)
        }
        if (nextHighTime != null) {
            o.put("nextHighTime", nextHighTime)
            if (nextHighM != null && nextHighM.isFinite()) o.put("nextHighM", nextHighM)
        }
        if (nextLowTime != null) {
            o.put("nextLowTime", nextLowTime)
            if (nextLowM != null && nextLowM.isFinite()) o.put("nextLowM", nextLowM)
        }
        return o
    }

    companion object {
        fun parseOpenMeteo(
            forecastBody: String,
            lat: Double,
            lon: Double,
            marineBody: String? = null,
            nowEpochMs: Long = System.currentTimeMillis(),
        ): WeatherSnapshot {
            val root = JSONObject(forecastBody)
            val cur = root.optJSONObject("current")
                ?: error("no current block")
            val code = cur.optInt("weather_code", -1)
            val tides = marineBody?.let { TideSeries.parseMarine(it, nowEpochMs) }
            return WeatherSnapshot(
                tempC = cur.optDouble("temperature_2m", Double.NaN),
                weatherCode = code,
                label = labelFor(code),
                precipMm = cur.optDouble("precipitation", 0.0),
                windMps = cur.optDouble("wind_speed_10m", 0.0),
                lat = lat,
                lon = lon,
                seaLevelM = tides?.seaLevelM,
                nextHighTime = tides?.nextHighTime,
                nextHighM = tides?.nextHighM,
                nextLowTime = tides?.nextLowTime,
                nextLowM = tides?.nextLowM,
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

/**
 * Next high/low from Open-Meteo marine hourly `sea_level_height_msl`.
 * Model is coarse near coasts; not for navigation.
 */
data class TideSeries(
    val seaLevelM: Double?,
    val nextHighTime: String?,
    val nextHighM: Double?,
    val nextLowTime: String?,
    val nextLowM: Double?,
) {
    companion object {
        fun parseMarine(body: String, nowEpochMs: Long): TideSeries? {
            val root = JSONObject(body)
            val hourly = root.optJSONObject("hourly") ?: return null
            val times = hourly.optJSONArray("time") ?: return null
            val heights = hourly.optJSONArray("sea_level_height_msl") ?: return null
            val n = minOf(times.length(), heights.length())
            if (n < 3) return null

            val zone = try {
                ZoneId.of(root.optString("timezone", "UTC"))
            } catch (_: Throwable) {
                ZoneId.of("UTC")
            }

            data class Sample(val epochMs: Long, val hhmm: String, val heightM: Double)

            val samples = ArrayList<Sample>(n)
            for (i in 0 until n) {
                if (heights.isNull(i)) continue
                val h = heights.optDouble(i, Double.NaN)
                if (!h.isFinite()) continue
                val iso = times.optString(i)
                val epoch = isoLocalToEpochMs(iso, zone) ?: continue
                val hhmm = hhmmFromIso(iso) ?: continue
                samples.add(Sample(epoch, hhmm, h))
            }
            if (samples.size < 3) return null

            var nowIdx = 0
            while (nowIdx + 1 < samples.size && samples[nowIdx + 1].epochMs <= nowEpochMs) {
                nowIdx++
            }
            val seaNow = samples[nowIdx].heightM

            var highTime: String? = null
            var highM: Double? = null
            var lowTime: String? = null
            var lowM: Double? = null

            for (i in (nowIdx + 1) until (samples.size - 1)) {
                val prev = samples[i - 1].heightM
                val cur = samples[i].heightM
                val next = samples[i + 1].heightM
                if (highTime == null && cur >= prev && cur > next) {
                    highTime = samples[i].hhmm
                    highM = cur
                }
                if (lowTime == null && cur <= prev && cur < next) {
                    lowTime = samples[i].hhmm
                    lowM = cur
                }
                if (highTime != null && lowTime != null) break
            }

            return TideSeries(
                seaLevelM = seaNow,
                nextHighTime = highTime,
                nextHighM = highM,
                nextLowTime = lowTime,
                nextLowM = lowM,
            )
        }

        fun isoLocalToEpochMs(iso: String, zone: ZoneId): Long? {
            if (iso.length < 16) return null
            return try {
                LocalDateTime.parse(iso.take(16)).atZone(zone).toInstant().toEpochMilli()
            } catch (_: Throwable) {
                null
            }
        }

        fun hhmmFromIso(iso: String): String? {
            if (iso.length < 16) return null
            return iso.substring(11, 16)
        }

        /** Test helper — format an epoch in a given zone as HH:MM. */
        fun hhmmFromEpoch(epochMs: Long, zone: ZoneId = ZoneId.systemDefault()): String {
            val z = Instant.ofEpochMilli(epochMs).atZone(zone)
            return String.format(Locale.US, "%02d:%02d", z.hour, z.minute)
        }
    }
}
