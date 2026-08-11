package slate.app.theme

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate

/**
 * Phone-app light/dark preference. Independent of the watch face chrome colours
 * in [slate.app.settings.WatchUiTheme].
 *
 * Uses AppCompat local + default night mode so Material3 resource colours swap
 * on [AppCompatActivity] subclasses (not only system bar contrast).
 */
enum class AppThemeMode {
    Light,
    Dark,
}

object AppThemePrefs {
    private const val PREFS = "slate_app_theme"
    private const val KEY_MODE = "mode"

    fun mode(context: Context): AppThemeMode {
        val raw = prefs(context).getString(KEY_MODE, AppThemeMode.Dark.name)
        return runCatching { AppThemeMode.valueOf(raw!!) }.getOrDefault(AppThemeMode.Dark)
    }

    fun setMode(context: Context, mode: AppThemeMode) {
        prefs(context).edit().putString(KEY_MODE, mode.name).commit()
        val night = nightMode(mode)
        AppCompatDelegate.setDefaultNightMode(night)
        val activity = context.findActivity()
        if (activity is AppCompatActivity) {
            activity.delegate.localNightMode = night
        } else {
            activity?.recreate()
        }
    }

    fun toggle(context: Context): AppThemeMode {
        val next = when (mode(context)) {
            AppThemeMode.Light -> AppThemeMode.Dark
            AppThemeMode.Dark -> AppThemeMode.Light
        }
        setMode(context, next)
        return next
    }

    fun applySaved(context: Context) {
        AppCompatDelegate.setDefaultNightMode(nightMode(mode(context)))
    }

    private fun nightMode(mode: AppThemeMode): Int =
        when (mode) {
            AppThemeMode.Light -> AppCompatDelegate.MODE_NIGHT_NO
            AppThemeMode.Dark -> AppCompatDelegate.MODE_NIGHT_YES
        }

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    private fun Context.findActivity(): Activity? {
        var ctx: Context = this
        while (ctx is ContextWrapper) {
            if (ctx is Activity) return ctx
            ctx = ctx.baseContext
        }
        return null
    }
}
