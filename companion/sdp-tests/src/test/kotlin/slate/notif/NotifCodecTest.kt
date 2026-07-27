package slate.notif

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class NotifCodecTest {
    @Test
    fun upsert_roundTripLayout() {
        val bytes = SystemNotifCodec.encodeUpsert(
            key = "pkg|0|1",
            category = NotifIconCategory.MESSAGE.atlasId,
            monogram = 'W',
            title = "Alice",
            text = "Hello",
            whenEpochSec = 1_700_000_000L,
            ongoing = false,
            clearable = true,
        )
        assertEquals(SystemNotifCodec.OP_UPSERT, bytes[0].toInt() and 0xFF)
        val keyLen = bytes[1].toInt() and 0xFF
        assertEquals("pkg|0|1".toByteArray().size, keyLen)
        var i = 2 + keyLen
        val flags = bytes[i++].toInt() and 0xFF
        assertEquals(SystemNotifCodec.FLAG_CLEARABLE, flags)
        assertEquals(NotifIconCategory.MESSAGE.atlasId, bytes[i++].toInt() and 0xFF)
        assertEquals('W'.code, bytes[i++].toInt() and 0xFF)
        val titleLen = bytes[i++].toInt() and 0xFF
        assertEquals(5, titleLen)
        i += titleLen
        val textLen = bytes[i++].toInt() and 0xFF
        assertEquals(5, textLen)
        i += textLen
        assertEquals(4, bytes.size - i) // u32 when
    }

    @Test
    fun remove_and_clear() {
        val rem = SystemNotifCodec.encodeRemove("k")
        assertEquals(SystemNotifCodec.OP_REMOVE, rem[0].toInt() and 0xFF)
        assertEquals(1, rem[1].toInt() and 0xFF)
        assertEquals('k'.code.toByte(), rem[2])

        val clr = SystemNotifCodec.encodeClearAll()
        assertEquals(1, clr.size)
        assertEquals(SystemNotifCodec.OP_CLEAR_ALL, clr[0].toInt() and 0xFF)
    }

    @Test
    fun icon_mapper_message_and_monogram() {
        val ref = NotifIconMapper.map("com.whatsapp", "Bob")
        assertEquals(NotifIconCategory.MESSAGE, ref.category)
        assertEquals('B', ref.monogram)

        val generic = NotifIconMapper.map("com.example.foo", null)
        assertEquals(NotifIconCategory.GENERIC, generic.category)
        assertTrue(generic.monogram.isLetterOrDigit() || generic.monogram == '?')
    }
}
