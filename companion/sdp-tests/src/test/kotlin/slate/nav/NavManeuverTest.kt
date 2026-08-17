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
        assertTrue(j.contains("\"destinationDistanceM\":5200"))
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

    @Test
    fun turnTypesFromOsmAndInts() {
        assertEquals("left", OsmAndTurnTypes.toTurn(OsmAndTurnTypes.TL))
        assertEquals("slight_right", OsmAndTurnTypes.toTurn(OsmAndTurnTypes.TSLR))
        assertEquals("u_turn", OsmAndTurnTypes.toTurn(OsmAndTurnTypes.TU))
        assertEquals("roundabout", OsmAndTurnTypes.toTurn(OsmAndTurnTypes.RNDB))
    }

    @Test
    fun parsesOsmAndNotification() {
        val p = OsmAndNotifParser.parse(
            title = "250 m • Turn left",
            text = "Main Street\n5.2 km • 12 min • 14:32",
        )!!
        assertEquals("left", p.turn)
        assertEquals(250, p.distanceToTurnM)
        assertEquals(5200, p.destinationDistanceM)
        assertEquals("Main Street", p.street)
    }
}
