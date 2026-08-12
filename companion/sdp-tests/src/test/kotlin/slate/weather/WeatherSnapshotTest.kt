package slate.weather

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import java.time.LocalDateTime
import java.time.ZoneId

class WeatherSnapshotTest {
    @Test
    fun parsesOpenMeteoCurrent() {
        val body = """
            {
              "current": {
                "temperature_2m": 18.5,
                "weather_code": 3,
                "precipitation": 0.2,
                "wind_speed_10m": 4.1
              }
            }
        """.trimIndent()
        val snap = WeatherSnapshot.parseOpenMeteo(body, 48.85, 2.35)
        assertEquals(18.5, snap.tempC)
        assertEquals(3, snap.weatherCode)
        assertEquals("Cloudy", snap.label)
        assertEquals(0.2, snap.precipMm)
        assertEquals(4.1, snap.windMps)
        assertEquals(48.85, snap.lat)
        assertNull(snap.seaLevelM)
        val json = snap.toJson()
        assertEquals("snapshot", json.getString("type"))
        assertTrue(json.getString("label").isNotBlank())
        assertTrue(!json.has("seaLevelM"))
    }

    @Test
    fun parsesMarineTidesIntoSnapshot() {
        val zone = ZoneId.of("Indian/Mahe")
        // Fixed "now" mid-series so next high/low are deterministic.
        val now = LocalDateTime.of(2026, 8, 12, 12, 0).atZone(zone).toInstant().toEpochMilli()
        val marine = """
            {
              "timezone": "Indian/Mahe",
              "hourly": {
                "time": [
                  "2026-08-12T10:00",
                  "2026-08-12T11:00",
                  "2026-08-12T12:00",
                  "2026-08-12T13:00",
                  "2026-08-12T14:00",
                  "2026-08-12T15:00",
                  "2026-08-12T16:00",
                  "2026-08-12T17:00",
                  "2026-08-12T18:00"
                ],
                "sea_level_height_msl": [
                  0.10, 0.20, 0.30, 0.50, 0.70, 0.55, 0.40, 0.25, 0.35
                ]
              }
            }
        """.trimIndent()
        val forecast = """
            {
              "current": {
                "temperature_2m": 28.0,
                "weather_code": 0,
                "precipitation": 0.0,
                "wind_speed_10m": 3.0
              }
            }
        """.trimIndent()
        val snap = WeatherSnapshot.parseOpenMeteo(
            forecast, -4.68, 55.48, marine, now,
        )
        assertEquals(0.30, snap.seaLevelM!!, 0.001)
        assertEquals("Clear", snap.label)
        // Peak at 14:00 (0.70), trough at 17:00 (0.25) after noon.
        assertEquals("14:00", snap.nextHighTime)
        assertEquals(0.70, snap.nextHighM!!, 0.001)
        assertEquals("17:00", snap.nextLowTime)
        assertEquals(0.25, snap.nextLowM!!, 0.001)
        val json = snap.toJson()
        assertEquals("14:00", json.getString("nextHighTime"))
        assertTrue(json.has("seaLevelM"))
    }

    @Test
    fun marineNullHeightsYieldNoTides() {
        val marine = """
            {
              "timezone": "UTC",
              "hourly": {
                "time": ["2026-08-12T00:00", "2026-08-12T01:00", "2026-08-12T02:00"],
                "sea_level_height_msl": [null, null, null]
              }
            }
        """.trimIndent()
        assertNull(TideSeries.parseMarine(marine, 0L))
    }

    @Test
    fun labelsCoverCommonCodes() {
        assertEquals("Clear", WeatherSnapshot.labelFor(0))
        assertEquals("Rain", WeatherSnapshot.labelFor(61))
        assertEquals("Thunder", WeatherSnapshot.labelFor(95))
        assertNotNull(WeatherSnapshot.labelFor(3))
    }
}
