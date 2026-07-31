package slate.camera

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class Rgb332Test {
    @Test
    fun blackAndWhite() {
        assertEquals(0x00, Rgb332.fromArgb(0xff000000.toInt()))
        // 0xffffff → R=7 G=7 B=3 → 0b111_111_11 = 0xff
        assertEquals(0xff, Rgb332.fromArgb(0xffffffff.toInt()))
    }

    @Test
    fun downscale60x60Size() {
        val src = IntArray(120 * 120) { 0xffff0000.toInt() }
        val out = Rgb332.downscaleArgbToRgb332(src, 120, 120, 60, 60)
        assertEquals(3600, out.size)
        // red ≈ R=7 G=0 B=0
        assertEquals(0xe0.toByte(), out[0])
    }
}

class FrameGovernorTest {
    @Test
    fun dropsWhenOverBudget() {
        val g = FrameGovernor(budgetBytesPerSec = 3600, maxInFlight = 2)
        assertTrue(g.tryAccept(3600, 1000L))
        assertFalse(g.tryAccept(3600, 1000L)) // same second, over budget
        assertEquals(1L, g.accepted)
        assertEquals(1L, g.dropped)
    }

    @Test
    fun dropsWhenInFlightFull() {
        val g = FrameGovernor(budgetBytesPerSec = 100_000, maxInFlight = 1)
        assertTrue(g.tryAccept(100, 0L))
        assertFalse(g.tryAccept(100, 0L))
        g.onFrameAcked()
        assertTrue(g.tryAccept(100, 0L))
    }

    @Test
    fun fpsHintFor60x60() {
        val g = FrameGovernor(budgetBytesPerSec = 36_000)
        assertEquals(10.0, g.measuredFpsHint(3600), 0.01)
    }
}
