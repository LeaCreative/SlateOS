package slate.notif

import java.nio.charset.StandardCharsets

/**
 * Channel-4 SYSTEM codec for the notification retained store.
 * Firmware M10 will interpret these; until then the phone still sends them so
 * the wire path is ready and dumps are inspectable.
 *
 * All strings are UTF-8, length-prefixed with u8 (max 255).
 */
object SystemNotifCodec {
    const val OP_UPSERT: Int = 0x01
    const val OP_REMOVE: Int = 0x02
    const val OP_CLEAR_ALL: Int = 0x03

    const val FLAG_ONGOING: Int = 1 shl 0
    const val FLAG_CLEARABLE: Int = 1 shl 1

    fun encodeUpsert(
        key: String,
        category: Int,
        monogram: Char,
        title: String,
        text: String,
        whenEpochSec: Long,
        ongoing: Boolean,
        clearable: Boolean,
    ): ByteArray {
        val keyB = utf8(key, 64)
        val titleB = utf8(title, 48)
        val textB = utf8(text, 96)
        var flags = 0
        if (ongoing) flags = flags or FLAG_ONGOING
        if (clearable) flags = flags or FLAG_CLEARABLE
        val out = ArrayList<Byte>(8 + keyB.size + titleB.size + textB.size)
        out += OP_UPSERT.toByte()
        out += keyB.size.toByte()
        keyB.forEach { out += it }
        out += flags.toByte()
        out += (category and 0xFF).toByte()
        out += monogram.code.coerceIn(0, 127).toByte()
        out += titleB.size.toByte()
        titleB.forEach { out += it }
        out += textB.size.toByte()
        textB.forEach { out += it }
        val sec = whenEpochSec.coerceIn(0, 0xFFFF_FFFFL)
        out += (sec and 0xFF).toByte()
        out += ((sec shr 8) and 0xFF).toByte()
        out += ((sec shr 16) and 0xFF).toByte()
        out += ((sec shr 24) and 0xFF).toByte()
        return out.toByteArray()
    }

    fun encodeRemove(key: String): ByteArray {
        val keyB = utf8(key, 64)
        val out = ByteArray(2 + keyB.size)
        out[0] = OP_REMOVE.toByte()
        out[1] = keyB.size.toByte()
        System.arraycopy(keyB, 0, out, 2, keyB.size)
        return out
    }

    fun encodeClearAll(): ByteArray = byteArrayOf(OP_CLEAR_ALL.toByte())

    private fun utf8(s: String, max: Int): ByteArray {
        val raw = s.toByteArray(StandardCharsets.UTF_8)
        return if (raw.size <= max) raw else raw.copyOf(max)
    }
}
