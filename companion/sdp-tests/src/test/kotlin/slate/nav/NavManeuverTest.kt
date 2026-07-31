package slate.nav

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class NavManeuverTest {
    @Test
    fun jsonIncludesTypeAndStatus() {
        val m = NavManeuver.demo("left")
        val j = m.toJson()
        assertTrue(j.contains("\"type\":\"maneuver\""))
        assertTrue(j.contains("\"turn\":\"left\""))
        assertTrue(j.contains("\"status\":\"ok\""))
    }

    @Test
    fun lostGpsKeepsTurn() {
        val base = NavManeuver.demo("right")
        val lost = NavManeuver.lostGps(base)
        assertEquals("right", lost.turn)
        assertEquals("lost_gps", lost.status)
    }

    @Test
    fun disconnectedMarksStatus() {
        val d = NavManeuver.disconnected(NavManeuver.demo("straight"))
        assertEquals("disconnected", d.status)
        assertEquals("Phone disconnected", d.street)
    }
}
