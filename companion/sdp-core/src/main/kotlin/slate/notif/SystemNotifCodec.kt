package slate.notif

import java.nio.charset.StandardCharsets

/**
 * Channel-4 SYSTEM codec for notification stubs, on-demand bodies, and call alerts.
 *
 * All strings are UTF-8, length-prefixed with u8 (max 255, clipped by helpers).
 */
object SystemNotifCodec {
    const val OP_UPSERT: Int = 0x01
    const val OP_REMOVE: Int = 0x02
    const val OP_CLEAR_ALL: Int = 0x03
    const val OP_BODY: Int = 0x04
    const val OP_CALL_ALERT: Int = 0x06
    const val OP_CALL_END: Int = 0x07

    const val FLAG_ONGOING: Int = 1 shl 0
    const val FLAG_CLEARABLE: Int = 1 shl 1
    /** Bulk reconnect sync — watch retains without wake/haptic. */
    const val FLAG_SILENT: Int = 1 shl 2

    const val INPUT_NOTIF_REQ: Int = 0xE2

    fun encodeUpsert(
        key: String,
        category: Int,
        monogram: Char,
        title: String,
        text: String = "",
        whenEpochSec: Long,
        ongoing: Boolean,
        clearable: Boolean,
        silent: Boolean = false,
    ): ByteArray {
        val keyB = utf8(key, 64)
        val titleB = utf8(title, 48)
        val textB = utf8(text, 96)
        var flags = 0
        if (ongoing) flags = flags or FLAG_ONGOING
        if (clearable) flags = flags or FLAG_CLEARABLE
        if (silent) flags = flags or FLAG_SILENT
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

    fun encodeBody(key: String, text: String): ByteArray {
        val keyB = utf8(key, 64)
        val textB = utf8(text, 96)
        val out = ByteArray(3 + keyB.size + textB.size)
        out[0] = OP_BODY.toByte()
        out[1] = keyB.size.toByte()
        System.arraycopy(keyB, 0, out, 2, keyB.size)
        out[2 + keyB.size] = textB.size.toByte()
        System.arraycopy(textB, 0, out, 3 + keyB.size, textB.size)
        return out
    }

    fun encodeCallAlert(caller: String): ByteArray {
        val c = utf8(caller, 48)
        val out = ByteArray(2 + c.size)
        out[0] = OP_CALL_ALERT.toByte()
        out[1] = c.size.toByte()
        System.arraycopy(c, 0, out, 2, c.size)
        return out
    }

    fun encodeCallEnd(): ByteArray = byteArrayOf(OP_CALL_END.toByte())

    /** Decode watch → phone NOTIF_REQ. Returns key or null. */
    fun decodeNotifReq(msg: ByteArray): String? {
        if (msg.isEmpty() || (msg[0].toInt() and 0xFF) != INPUT_NOTIF_REQ) return null
        if (msg.size < 2) return null
        val klen = msg[1].toInt() and 0xFF
        if (msg.size < 2 + klen) return null
        return String(msg, 2, klen, StandardCharsets.UTF_8)
    }

    private fun utf8(s: String, max: Int): ByteArray {
        val raw = s.toByteArray(StandardCharsets.UTF_8)
        return if (raw.size <= max) raw else raw.copyOf(max)
    }
}
