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

    /**
     * The verdict must not name a culprit it cannot identify.
     *
     * `getConnectedDevices(GATT)` proves only that this phone holds a link
     * Slate does not own. That is equally true of a link left open with no
     * owner at all, which is what a reinstall leaves behind and what actually
     * happened on 9 Aug — the phone's stack log read "No ACL holders" while the
     * app told the operator another app was to blame. They went looking for
     * apps that were not running.
     */
    @Test
    fun heldOnThisPhoneDoesNotBlameAnotherApp() {
        val v = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = false,
                gattConnectedOnPhone = true,
                bonded = true,
            ),
        )
        assertFalse(
            v.summary.contains("another app", ignoreCase = true),
            "summary must not assert an owner the signal cannot identify: '${v.summary}'",
        )
        assertTrue(
            v.summary.contains("Slate does not own", ignoreCase = true),
            "summary should say what is actually known: '${v.summary}'",
        )
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
