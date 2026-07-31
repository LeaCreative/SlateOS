package slate.app.ota

import android.content.Context
import android.content.Intent
import android.net.Uri

/**
 * Remembers a picked DFU package across activity death.
 *
 * Both installer screens can be torn down mid-sequence — the CDM confirmation
 * dialog in particular takes the app out of the foreground and drops the user back
 * on MainActivity — so a selection held only in the activity instance, or only in
 * savedInstanceState, is gone by the time the user reaches the install button.
 *
 * A remembered URI is only worth anything while the persisted read grant behind it
 * is still held, so [restore] verifies that and forgets the URI otherwise.
 */
class DfuPackageSelection(private val context: Context, prefsName: String) {

    private val prefs = context.getSharedPreferences(prefsName, Context.MODE_PRIVATE)

    /** Takes a persistable read grant and remembers the URI only if that worked. */
    fun remember(uri: Uri) {
        val persisted = runCatching {
            context.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
        }.isSuccess
        if (persisted) {
            prefs.edit().putString(KEY_PACKAGE_URI, uri.toString()).apply()
        }
    }

    fun restore(): Uri? {
        val raw = prefs.getString(KEY_PACKAGE_URI, null) ?: return null
        val uri = Uri.parse(raw)
        val held = context.contentResolver.persistedUriPermissions.any {
            it.uri == uri && it.isReadPermission
        }
        if (held) {
            return uri
        }
        clear()
        return null
    }

    fun clear() {
        prefs.edit().remove(KEY_PACKAGE_URI).apply()
    }

    private companion object {
        const val KEY_PACKAGE_URI = "package_uri"
    }
}
