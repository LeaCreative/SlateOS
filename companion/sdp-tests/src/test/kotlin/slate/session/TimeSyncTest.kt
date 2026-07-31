package slate.session

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals

class TimeSyncTest {
    @Test
    fun encodeUnixLayout() {
        val epoch = 1_700_000_000L
        val bytes = TimeSync.encodeUnix(epoch)
        assertEquals(5, bytes.size)
        assertEquals(TimeSync.TIME_SYNC_OP, bytes[0].toInt() and 0xFF)
        assertContentEquals(
            byteArrayOf(0x20, 0x00, 0xF1.toByte(), 0x53, 0x65),
            bytes,
        )
    }
}
