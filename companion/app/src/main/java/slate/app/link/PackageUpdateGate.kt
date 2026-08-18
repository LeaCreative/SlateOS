package slate.app.link

import android.content.Context
import slate.app.BuildConfig

/**
 * Detects that this process started after an APK replace (`installDebug` /
 * Play update). The JS sandbox isolated process can outlive the old UI for
 * a moment; binding immediately deadlocks Main if teardown waits on that bind.
 */
object PackageUpdateGate {
    @Volatile
    var replacedThisProcess: Boolean = false
        private set

    fun onApplicationCreate(context: Context) {
        val prefs = context.applicationContext.getSharedPreferences(
            PREFS,
            Context.MODE_PRIVATE,
        )
        val last = prefs.getInt(KEY_VERSION_CODE, -1)
        val now = BuildConfig.VERSION_CODE
        replacedThisProcess = last != -1 && last != now
        if (last != now) {
            prefs.edit().putInt(KEY_VERSION_CODE, now).apply()
        }
        if (replacedThisProcess) {
            LinkLog.i("package replaced $last -> $now — defer JS sandbox bind")
        }
    }

    private const val PREFS = "slate_process"
    private const val KEY_VERSION_CODE = "version_code"
}
