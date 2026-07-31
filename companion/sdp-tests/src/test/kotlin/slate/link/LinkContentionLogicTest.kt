package slate.link

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class LinkContentionLogicTest {
    @Test
    fun clearWhenWeHoldLink() {
        val v = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = true,
                gattConnectedOnPhone = true,
                bonded = true,
            ),
        )
        assertFalse(v.blocked)
        assertEquals(LinkContentionLogic.Kind.Clear, v.kind)
    }

    @Test
    fun heldOnThisPhoneWhenGattElsewhere() {
        val v = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = false,
                gattConnectedOnPhone = true,
                bonded = true,
            ),
        )
        assertTrue(v.blocked)
        assertEquals(LinkContentionLogic.Kind.HeldOnThisPhone, v.kind)
    }

    @Test
    fun foreignWhenBondedNotAdvertising() {
        val v = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = false,
                gattConnectedOnPhone = false,
                bonded = true,
                advertisingSeen = false,
            ),
        )
        assertTrue(v.blocked)
        assertEquals(LinkContentionLogic.Kind.LikelyForeignCentral, v.kind)
    }

    @Test
    fun foreignWhenConnectFailsWhileBonded() {
        val v = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = false,
                gattConnectedOnPhone = false,
                bonded = true,
                connectFailedWhileBonded = true,
            ),
        )
        assertTrue(v.blocked)
        assertEquals(LinkContentionLogic.Kind.LikelyForeignCentral, v.kind)
    }

    @Test
    fun clearWhenUnknownAdvAndNoGatt() {
        val v = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = false,
                gattConnectedOnPhone = false,
                bonded = true,
                advertisingSeen = null,
            ),
        )
        assertFalse(v.blocked)
    }

    @Test
    fun clearWhenAdvertisingSeen() {
        val v = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = false,
                gattConnectedOnPhone = false,
                bonded = true,
                advertisingSeen = true,
            ),
        )
        assertFalse(v.blocked)
    }
}
