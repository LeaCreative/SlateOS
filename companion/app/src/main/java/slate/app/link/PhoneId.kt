package slate.app.link

import android.content.Context
import java.security.SecureRandom

/** Stable 8-byte phone id persisted for session HELLO_ACCEPT. */
object PhoneId {
    fun load(context: Context): ByteArray {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val hex = prefs.getString(KEY, null)
        if (hex != null && hex.length == PHONE_ID_HEX_LEN) {
            return decodeHex(hex)
        }
        val id = ByteArray(8)
        SecureRandom().nextBytes(id)
        prefs.edit().putString(KEY, encodeHex(id)).apply()
        return id
    }

    private fun encodeHex(bytes: ByteArray): String =
        bytes.joinToString("") { "%02x".format(it.toInt() and 0xFF) }

    private fun decodeHex(hex: String): ByteArray {
        val out = ByteArray(hex.length / 2)
        for (i in out.indices) {
            val j = i * 2
            out[i] = hex.substring(j, j + 2).toInt(16).toByte()
        }
        return out
    }

    private const val PREFS = "slate_phone_id"
    private const val KEY = "id"
    private const val PHONE_ID_HEX_LEN = 16
}
