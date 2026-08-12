package slate.notif

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class NotifCodecTest {
    @Test
    fun upsert_stub_empty_text() {
        val bytes = SystemNotifCodec.encodeUpsert(
            key = "pkg|0|1",
            category = NotifIconCategory.MESSAGE.atlasId,
            monogram = 'W',
            title = "WhatsApp",
            text = "",
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
        assertEquals(8, titleLen)
        i += titleLen
        val textLen = bytes[i++].toInt() and 0xFF
        assertEquals(0, textLen)
        assertEquals(4, bytes.size - i) // u32 when
    }

    @Test
    fun upsert_silent_flag() {
        val bytes = SystemNotifCodec.encodeUpsert(
            key = "k",
            category = 0,
            monogram = 'A',
            title = "t",
            whenEpochSec = 1L,
            ongoing = false,
            clearable = true,
            silent = true,
        )
        val keyLen = bytes[1].toInt() and 0xFF
        val flags = bytes[2 + keyLen].toInt() and 0xFF
        assertEquals(
            SystemNotifCodec.FLAG_CLEARABLE or SystemNotifCodec.FLAG_SILENT,
            flags,
        )
    }

    @Test
    fun body_and_call_ops() {
        val body = SystemNotifCodec.encodeBody("k1", "Hello body")
        assertEquals(SystemNotifCodec.OP_BODY, body[0].toInt() and 0xFF)
        assertEquals(2, body[1].toInt() and 0xFF)

        val alert = SystemNotifCodec.encodeCallAlert("Alice")
        assertEquals(SystemNotifCodec.OP_CALL_ALERT, alert[0].toInt() and 0xFF)
        assertEquals(5, alert[1].toInt() and 0xFF)

        val end = SystemNotifCodec.encodeCallEnd()
        assertEquals(SystemNotifCodec.OP_CALL_END, end[0].toInt() and 0xFF)
    }

    @Test
    fun notif_req_roundtrip() {
        val key = "pkg|42"
        val msg = ByteArray(2 + key.length)
        msg[0] = SystemNotifCodec.INPUT_NOTIF_REQ.toByte()
        msg[1] = key.length.toByte()
        key.toByteArray().copyInto(msg, 2)
        assertEquals(key, SystemNotifCodec.decodeNotifReq(msg))
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
