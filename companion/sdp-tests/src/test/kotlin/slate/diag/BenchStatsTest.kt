package slate.diag

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class BenchStatsTest {
    @Test
    fun percentiles_and_mean() {
        val s = BenchStats.summarize(listOf(10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0))
        assertEquals(10, s.n)
        assertEquals(55.0, s.mean, 0.01)
        assertEquals(55.0, s.p50, 0.01)
        assertTrue(s.p95 >= 90.0)
        assertTrue(s.p99 >= 95.0)
    }

    @Test
    fun diag_thru_roundtrip_bytes() {
        val start = SdpDiag.thruStart(100)
        assertEquals(SdpDiag.OP_THRU_START, start[0].toInt() and 0xFF)
        assertEquals(100L, SdpDiag.getU32(start, 1))
    }
}
