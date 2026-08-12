package slate.app.settings

import android.content.Context

/**
 * Phone-only bridge preferences (not synced to the watch).
 */
class HostPrefs(context: Context) {
    private val prefs =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** `clock` = AlarmClock intent; `exact` = Slate AlarmManager. */
    var alarmBackend: String
        get() = prefs.getString(KEY_ALARM_BACKEND, BACKEND_CLOCK) ?: BACKEND_CLOCK
        set(v) = prefs.edit().putString(
            KEY_ALARM_BACKEND,
            if (v == BACKEND_EXACT) BACKEND_EXACT else BACKEND_CLOCK,
        ).apply()

    /** Write watch steps/BPM into Health Connect when grants exist. */
    var healthConnectSync: Boolean
        get() = prefs.getBoolean(KEY_HC_SYNC, true)
        set(v) = prefs.edit().putBoolean(KEY_HC_SYNC, v).apply()

    companion object {
        private const val PREFS = "slate_host"
        private const val KEY_ALARM_BACKEND = "alarm_backend"
        private const val KEY_HC_SYNC = "health_connect_sync"
        const val BACKEND_CLOCK = "clock"
        const val BACKEND_EXACT = "exact"
    }
}
