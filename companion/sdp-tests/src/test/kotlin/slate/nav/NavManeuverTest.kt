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

    @Test
    fun parsesOsmAndRoundaboutNotification() {
        val p = OsmAndNotifParser.parse(
            title = "200 m • Take 2 exit and go",
            text = "Take 2 exit and go 500 m\n5.5 km • 6 min • 15:46 • 29 km/h",
        )!!
        assertEquals("roundabout", p.turn)
        assertEquals(200, p.distanceToTurnM)
        assertEquals(5500, p.destinationDistanceM)
        assertEquals(2, p.roundaboutExit)
        assertEquals("", p.street)
    }

    @Test
    fun continueToDestinationIsStraightNotArrive() {
        assertEquals("straight", OsmAndTurnTypes.fromPhrase("Continue to destination"))
        assertEquals("straight", OsmAndTurnTypes.fromPhrase("Go ahead and arrive at destination"))
        assertEquals("arrive", OsmAndTurnTypes.fromPhrase("You have arrived"))
        assertEquals("arrive", OsmAndTurnTypes.fromPhrase("Arrived at destination"))
        assertEquals("arrive", OsmAndTurnTypes.fromPhrase("Destination reached"))
    }

    @Test
    fun parsesArrivalNotificationWithoutDistances() {
        val p = OsmAndNotifParser.parse(
            title = "You have arrived",
            text = "Home",
        )!!
        assertEquals("arrive", p.turn)
        assertEquals(0, p.distanceToTurnM)
        assertEquals(false, p.hasDestination)
    }

    @Test
    fun summaryZeroMetresIsARealDestination() {
        val p = OsmAndNotifParser.parse(
            title = "0 m • Go ahead",
            text = "Home\n0 m • 0 min • 14:32",
        )!!
        assertEquals("straight", p.turn)
        assertEquals(0, p.distanceToTurnM)
        assertEquals(0, p.destinationDistanceM)
        assertEquals(true, p.hasDestination)
    }

    @Test
    fun promoteArriveOnLastGoAheadStretch() {
        assertEquals(
            "arrive",
            OsmAndTurnTypes.promoteArrive("straight", 20, 25, sawDestination = true),
        )
        assertEquals(
            "left",
            OsmAndTurnTypes.promoteArrive("left", 20, 25, sawDestination = true),
        )
        assertEquals(
            "straight",
            OsmAndTurnTypes.promoteArrive("straight", 80, 2000, sawDestination = true),
        )
        assertEquals(
            "straight",
            OsmAndTurnTypes.promoteArrive("straight", 0, 0, sawDestination = false),
        )
    }

    @Test
    fun arrivalVoiceCommandIsReachedDestinationOnly() {
        assertEquals(
            true,
            OsmAndTurnTypes.isArrivalVoiceCommand(listOf("reached_destination")),
        )
        assertEquals(
            false,
            OsmAndTurnTypes.isArrivalVoiceCommand(listOf("and_arrive_destination")),
        )
    }

    @Test
    fun arriveDemoHasZeroRemaining() {
        val m = NavManeuver.demo("arrive")
        assertEquals("arrive", m.turn)
        assertEquals(0, m.distanceM)
        assertEquals(0, m.destinationDistanceM)
    }
}
