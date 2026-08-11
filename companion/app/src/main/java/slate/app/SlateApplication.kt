package slate.app

import android.app.Application
import slate.app.theme.AppThemePrefs

class SlateApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        AppThemePrefs.applySaved(this)
    }
}
