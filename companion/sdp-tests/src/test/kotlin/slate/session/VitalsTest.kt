package slate.session

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class VitalsTest {
    @Test
    fun roundTrip() {
        val enc = Vitals.encode(steps = 12345, bpm = 72)
        assertEquals(7, enc.size)
        assertEquals(Vitals.OP, enc[0].toInt() and 0xFF)
        val snap = Vitals.parse(enc)!!
        assertEquals(12345L, snap.steps)
        assertEquals(72, snap.bpm)
    }

    @Test
    fun rejectsShort() {
        assertNull(Vitals.parse(byteArrayOf(0xE2.toByte(), 1, 2)))
    }
}
