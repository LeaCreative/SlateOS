package slate.weather

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

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
        val json = snap.toJson()
        assertEquals("snapshot", json.getString("type"))
        assertTrue(json.getString("label").isNotBlank())
    }

    @Test
    fun labelsCoverCommonCodes() {
        assertEquals("Clear", WeatherSnapshot.labelFor(0))
        assertEquals("Rain", WeatherSnapshot.labelFor(61))
        assertEquals("Thunder", WeatherSnapshot.labelFor(95))
    }
}
