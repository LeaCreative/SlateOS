package slate.app.health

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertSame
import kotlin.test.assertTrue

class BpmLogLogicTest {
    @Test
    fun rejectsOutOfRange() {
        val empty = emptyList<BpmSample>()
        assertSame(empty, BpmLogLogic.append(empty, 1_000L, 0))
        assertSame(empty, BpmLogLogic.append(empty, 1_000L, 29))
        assertSame(empty, BpmLogLogic.append(empty, 1_000L, 221))
    }

    @Test
    fun keepsBpmChangeImmediately() {
        val t0 = 1_000_000L
        val one = BpmLogLogic.append(emptyList(), t0, 72)
        val two = BpmLogLogic.append(one, t0 + 1_000L, 80)
        assertEquals(listOf(BpmSample(t0, 72), BpmSample(t0 + 1_000L, 80)), two)
    }

    @Test
    fun downsamplesUnchangedBpmInsideInterval() {
        val t0 = 1_000_000L
        val one = BpmLogLogic.append(emptyList(), t0, 72)
        val same = BpmLogLogic.append(one, t0 + BpmLogLogic.MIN_INTERVAL_MS - 1, 72)
        assertSame(one, same)
        val later = BpmLogLogic.append(one, t0 + BpmLogLogic.MIN_INTERVAL_MS, 72)
        assertEquals(2, later.size)
        assertEquals(72, later.last().bpm)
    }

    @Test
    fun dropsSamplesOlderThan24h() {
        val now = 10L * BpmLogLogic.WINDOW_MS
        val old = BpmLogLogic.append(emptyList(), now - BpmLogLogic.WINDOW_MS - 1, 70, now)
        assertTrue(old.isEmpty())
        val kept = BpmLogLogic.append(emptyList(), now - 1_000L, 70, now)
        assertEquals(1, kept.size)
        val rolled = BpmLogLogic.append(kept, now, 71, now)
        assertEquals(listOf(BpmSample(now - 1_000L, 70), BpmSample(now, 71)), rolled)
    }

    @Test
    fun capsSampleCount() {
        var list = emptyList<BpmSample>()
        val t0 = 1_000_000L
        for (i in 0 until BpmLogLogic.MAX_SAMPLES + 25) {
            list = BpmLogLogic.append(
                list,
                t0 + i * BpmLogLogic.MIN_INTERVAL_MS,
                60 + (i % 40),
            )
        }
        assertEquals(BpmLogLogic.MAX_SAMPLES, list.size)
        assertEquals(t0 + 25 * BpmLogLogic.MIN_INTERVAL_MS, list.first().timeMs)
    }

    @Test
    fun serializeRoundTrip() {
        val samples = listOf(BpmSample(1_000L, 64), BpmSample(2_000L, 71))
        assertEquals(samples, BpmLogLogic.parse(BpmLogLogic.serialize(samples)))
    }
}
