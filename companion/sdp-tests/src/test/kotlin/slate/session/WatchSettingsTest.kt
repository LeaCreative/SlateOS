package slate.session

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * The phone half of settings sync must agree with the watch half exactly.
 *
 * Golden vectors match `slate::settings_sync::encode` (wire version 3).
 */
class WatchSettingsTest {
    @Test
    fun encodeMatchesFirmwareGoldenDefaults() {
        val bytes = WatchSettings.encode(
            WatchSettings.Payload(
                revision = 0L,
                tiltEnabled = true,
                wakeSeconds = 20,
                showSteps = true,
                showDiag = true,
                hrEnabled = false,
            ),
        )
        assertContentEquals(
            byteArrayOf(
                0x21, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x14, 0x01, 0x01, 0x00,
                0xFF.toByte(), 0xFF.toByte(),
                0xFF.toByte(), 0xFF.toByte(),
                0x10, 0x84.toByte(),
            ),
            bytes,
        )
    }

    @Test
    fun encodeMatchesFirmwareGoldenEverythingOff() {
        val bytes = WatchSettings.encode(
            WatchSettings.Payload(
                revision = 1L,
                tiltEnabled = false,
                wakeSeconds = 0,
                showSteps = false,
                showDiag = false,
                hrEnabled = false,
            ),
        )
        assertContentEquals(
            byteArrayOf(
                0x21, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0xFF.toByte(), 0xFF.toByte(),
                0xFF.toByte(), 0xFF.toByte(),
                0x10, 0x84.toByte(),
            ),
            bytes,
        )
    }

    @Test
    fun encodeMatchesFirmwareGoldenMixed() {
        val bytes = WatchSettings.encode(
            WatchSettings.Payload(
                revision = 7L,
                tiltEnabled = true,
                wakeSeconds = 120,
                showSteps = false,
                showDiag = false,
                hrEnabled = true,
                uiChrome = 0x07E0,
                faceBright = 0xF800,
                faceDim = 0x001F,
            ),
        )
        assertContentEquals(
            byteArrayOf(
                0x21, 0x03, 0x07, 0x00, 0x00, 0x00, 0x01, 0x78, 0x00, 0x00, 0x01,
                0xE0.toByte(), 0x07,
                0x00, 0xF8.toByte(),
                0x1F, 0x00,
            ),
            bytes,
        )
    }

    @Test
    fun encodeMatchesFirmwareGoldenHighRevision() {
        val bytes = WatchSettings.encode(
            WatchSettings.Payload(
                revision = 0xDEADBEEFL,
                tiltEnabled = false,
                wakeSeconds = 60,
                showSteps = true,
                showDiag = false,
                hrEnabled = true,
            ),
        )
        assertContentEquals(
            byteArrayOf(
                0x21, 0x03, 0xEF.toByte(), 0xBE.toByte(), 0xAD.toByte(), 0xDE.toByte(),
                0x00, 0x3C, 0x01, 0x00, 0x01,
                0xFF.toByte(), 0xFF.toByte(),
                0xFF.toByte(), 0xFF.toByte(),
                0x10, 0x84.toByte(),
            ),
            bytes,
        )
    }

    @Test
    fun decodesWhatTheFirmwareEncodes() {
        val fromWatch = byteArrayOf(
            0x21, 0x03, 0xEF.toByte(), 0xBE.toByte(), 0xAD.toByte(), 0xDE.toByte(),
            0x00, 0x3C, 0x01, 0x00, 0x01,
            0xFF.toByte(), 0xFF.toByte(),
            0xFF.toByte(), 0xFF.toByte(),
            0x10, 0x84.toByte(),
        )
        val p = WatchSettings.decode(fromWatch)
        assertNotNull(p)
        assertEquals(0xDEADBEEFL, p.revision)
        assertFalse(p.tiltEnabled)
        assertEquals(60, p.wakeSeconds)
        assertTrue(p.showSteps)
        assertFalse(p.showDiag)
        assertTrue(p.hrEnabled)
        assertEquals(0xFFFF, p.uiChrome)
        assertEquals(0xFFFF, p.faceBright)
        assertEquals(0x8410, p.faceDim)
    }

    @Test
    fun decodeRejectsShortBuffer() {
        assertNull(WatchSettings.decode(ByteArray(10)))
    }

    @Test
    fun decodeRejectsWrongOpcode() {
        val bytes = WatchSettings.encode(WatchSettings.Payload())
        bytes[0] = 0x10
        assertNull(WatchSettings.decode(bytes))
    }

    @Test
    fun decodeRejectsFutureWireVersion() {
        val bytes = WatchSettings.encode(WatchSettings.Payload())
        bytes[1] = (WatchSettings.WIRE_VERSION + 1).toByte()
        assertNull(WatchSettings.decode(bytes))
    }

    @Test
    fun differsIgnoresRevision() {
        val a = WatchSettings.Payload(revision = 1L, hrEnabled = true)
        val b = WatchSettings.Payload(revision = 99L, hrEnabled = true)
        assertFalse(WatchSettings.differs(a, b))
        assertTrue(WatchSettings.differs(a, b.copy(hrEnabled = false)))
        assertTrue(WatchSettings.differs(a, b.copy(uiChrome = 0x07E0)))
    }

    @Test
    fun conflictWatchWins() {
        val watch = WatchSettings.Payload(revision = 9L, tiltEnabled = true)
        val phone = WatchSettings.Payload(revision = 9L, tiltEnabled = false)
        assertFalse(WatchSettings.shouldApply(watch, phone, selfIsWatch = true))
        assertTrue(WatchSettings.shouldApply(phone, watch, selfIsWatch = false))
    }

    @Test
    fun nextRevisionIsLamport() {
        assertEquals(6L, WatchSettings.nextRevision(5L, 5L))
        assertEquals(7L, WatchSettings.nextRevision(5L, 6L))
        assertEquals(0xFFFFFFFFL, WatchSettings.nextRevision(0xFFFFFFFFL, 0L))
    }
}
