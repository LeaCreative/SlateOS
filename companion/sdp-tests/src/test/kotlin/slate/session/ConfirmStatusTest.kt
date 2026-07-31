package slate.session

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import slate.generated.SdpWire

class ConfirmStatusTest {
    @Test
    fun encodeRequestIsSingleByte() {
        assertContentEquals(
            byteArrayOf(SdpWire.ControlOp.CONFIRM_STATUS_REQUEST.toByte()),
            ConfirmStatus.encodeRequest(),
        )
    }

    @Test
    fun parseTrialCountdown() {
        val msg = byteArrayOf(
            SdpWire.ControlOp.CONFIRM_STATUS.toByte(),
            1,
            0x10, 0x27, 0x00, 0x00, // 10000 ms LE
        )
        val snap = ConfirmStatus.parse(msg)
        assertNotNull(snap)
        assertTrue(snap.needsConfirm)
        assertEquals(10_000L, snap.dwellMsRemaining)
    }

    @Test
    fun parseConfirmed() {
        val msg = byteArrayOf(
            SdpWire.ControlOp.CONFIRM_STATUS.toByte(),
            0,
            0, 0, 0, 0,
        )
        val snap = ConfirmStatus.parse(msg)
        assertNotNull(snap)
        assertFalse(snap.needsConfirm)
        assertEquals(0L, snap.dwellMsRemaining)
    }

    @Test
    fun parseRejectsShortOrWrongOp() {
        assertNull(ConfirmStatus.parse(byteArrayOf(0xE1.toByte(), 1, 0, 0)))
        assertNull(ConfirmStatus.parse(byteArrayOf(0xE0.toByte(), 1, 0, 0, 0, 0)))
    }

    @Test
    fun sessionClientStoresConfirmStatus() {
        val client = SessionClient(
            phoneId = ByteArray(8),
            hostVersion = "test",
        )
        client.onLinkUp()
        val result = client.onControlMessage(
            byteArrayOf(
                SdpWire.ControlOp.CONFIRM_STATUS.toByte(),
                1,
                0xE8.toByte(), 0x03, 0x00, 0x00, // 1000
            ),
        )
        assertTrue(result.outbound.isEmpty())
        assertEquals(true, client.confirmStatus?.needsConfirm)
        assertEquals(1000L, client.confirmStatus?.dwellMsRemaining)
        assertContentEquals(
            ConfirmStatus.encodeRequest(),
            client.encodeConfirmStatusRequest(),
        )
    }
}
