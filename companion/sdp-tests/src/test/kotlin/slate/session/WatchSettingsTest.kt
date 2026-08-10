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
 * Two separate things can go wrong and both are silent. The bytes can disagree,
 * in which case the watch rejects the message and the user's change on the phone
 * simply never happens. Or the *merge rule* can disagree, in which case both ends
 * accept each other forever and a setting flickers between two values.
 *
 * The golden vectors below were produced by running `slate::settings_sync::encode`
 * from `src/settings_sync.cpp` — they are the firmware's own output, not a
 * re-derivation of what it ought to be.
 */
class WatchSettingsTest {
    // ── wire parity with src/settings_sync.cpp ───────────────────────────────

    @Test
    fun encodeMatchesFirmwareGoldenDefaults() {
        val bytes = WatchSettings.encode(
            WatchSettings.Payload(
                revision = 0L,
                tiltEnabled = true,
                wakeSeconds = 20,
                showSteps = true,
                showDiag = true,
            ),
        )
        assertContentEquals(
            byteArrayOf(0x21, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x14, 0x01, 0x01),
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
            ),
        )
        assertContentEquals(
            byteArrayOf(0x21, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
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
            ),
        )
        assertContentEquals(
            byteArrayOf(0x21, 0x01, 0x07, 0x00, 0x00, 0x00, 0x01, 0x78, 0x00, 0x00),
            bytes,
        )
    }

    /** The revision is u32 little-endian, and Kotlin has no unsigned Int. */
    @Test
    fun encodeMatchesFirmwareGoldenHighRevision() {
        val bytes = WatchSettings.encode(
            WatchSettings.Payload(
                revision = 0xDEADBEEFL,
                tiltEnabled = false,
                wakeSeconds = 60,
                showSteps = true,
                showDiag = false,
            ),
        )
        assertContentEquals(
            byteArrayOf(0x21, 0x01, 0xEF.toByte(), 0xBE.toByte(), 0xAD.toByte(), 0xDE.toByte(), 0x00, 0x3C, 0x01, 0x00),
            bytes,
        )
    }

    @Test
    fun decodesWhatTheFirmwareEncodes() {
        val fromWatch = byteArrayOf(
            0x21, 0x01, 0xEF.toByte(), 0xBE.toByte(), 0xAD.toByte(), 0xDE.toByte(), 0x00, 0x3C, 0x01, 0x00,
        )
        val p = WatchSettings.decode(fromWatch)
        assertNotNull(p)
        // Above 0x7FFFFFFF this is where a naive Int decode would go negative.
        assertEquals(0xDEADBEEFL, p.revision)
        assertFalse(p.tiltEnabled)
        assertEquals(60, p.wakeSeconds)
        assertTrue(p.showSteps)
        assertFalse(p.showDiag)
    }

    @Test
    fun roundTripsEveryTimeoutChoice() {
        for (secs in WatchSettings.WAKE_SECONDS_CHOICES) {
            val p = WatchSettings.Payload(revision = 3L, wakeSeconds = secs)
            assertEquals(p, WatchSettings.decode(WatchSettings.encode(p)), "wakeSeconds=$secs")
        }
    }

    @Test
    fun rejectsRubbish() {
        val good = WatchSettings.encode(WatchSettings.Payload(revision = 1L))
        assertNull(WatchSettings.decode(good.copyOf(WatchSettings.PAYLOAD_BYTES - 1)), "short")

        val wrongOp = good.copyOf().also { it[0] = 0x10 }
        assertNull(WatchSettings.decode(wrongOp), "wrong opcode")

        val future = good.copyOf().also { it[1] = (WatchSettings.WIRE_VERSION + 1).toByte() }
        assertNull(WatchSettings.decode(future), "future wire version")
    }

    // ── the merge rule ───────────────────────────────────────────────────────

    @Test
    fun higherRevisionWins() {
        val mine = WatchSettings.Payload(revision = 4L, wakeSeconds = 20)
        val theirs = WatchSettings.Payload(revision = 5L, wakeSeconds = 60)
        assertTrue(WatchSettings.shouldApply(mine, theirs))
        assertFalse(WatchSettings.shouldApply(theirs, mine))
    }

    @Test
    fun identicalContentIsANoop() {
        val a = WatchSettings.Payload(revision = 7L, wakeSeconds = 20)
        val b = WatchSettings.Payload(revision = 7L, wakeSeconds = 20)
        assertFalse(WatchSettings.shouldApply(a, b))
    }

    /**
     * The case that decides whether the two ends settle or fight: both edited at
     * the same revision without seeing each other. Exactly one side must yield,
     * and each must reach that verdict on its own.
     */
    @Test
    fun conflictResolvesOneWayOnly() {
        val watch = WatchSettings.Payload(revision = 9L, tiltEnabled = true, wakeSeconds = 20, showSteps = true)
        val phone = WatchSettings.Payload(revision = 9L, tiltEnabled = false, wakeSeconds = 60, showSteps = false)

        val phoneTakesWatches = WatchSettings.shouldApply(phone, watch, selfIsWatch = false)
        val watchTakesPhones = WatchSettings.shouldApply(watch, phone, selfIsWatch = true)

        assertTrue(phoneTakesWatches, "the phone yields on a tie")
        assertFalse(watchTakesPhones, "the watch keeps its own on a tie")
        assertTrue(phoneTakesWatches != watchTakesPhones, "no ping-pong, no stalemate")
    }

    @Test
    fun lamportCounterMakesLastWriterWin() {
        val shared = 5L
        val watchRev = WatchSettings.nextRevision(shared, shared)
        assertEquals(6L, watchRev)
        // The phone adopts 6 from the watch, then the user edits on the phone.
        val phoneRev = WatchSettings.nextRevision(shared, watchRev)
        assertEquals(7L, phoneRev)

        val fromWatch = WatchSettings.Payload(revision = watchRev, wakeSeconds = 20)
        val fromPhone = WatchSettings.Payload(revision = phoneRev, wakeSeconds = 90)
        assertFalse(
            WatchSettings.shouldApply(fromPhone, fromWatch),
            "the phone's later edit must not be undone by the watch's earlier one",
        )
    }

    @Test
    fun revisionSaturatesRatherThanWrapping() {
        assertEquals(0xFFFFFFFFL, WatchSettings.nextRevision(0xFFFFFFFFL, 0L))
        assertEquals(0xFFFFFFFFL, WatchSettings.nextRevision(0L, 0xFFFFFFFFL))
    }

    /**
     * The timeout choices must match the ones the watch's own settings row
     * cycles through (`kChoices` in `Core::on_tap_elem`), or a value picked on
     * the phone would be one the watch can display but never return to.
     */
    @Test
    fun timeoutChoicesMatchTheWatchRow() {
        assertContentEquals(
            listOf(10, 20, 30, 60, 120, 0),
            WatchSettings.WAKE_SECONDS_CHOICES,
        )
    }
}
