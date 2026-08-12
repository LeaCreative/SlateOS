package slate.app.settings

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.LinearLayout
import android.widget.TextView
import androidx.health.connect.client.PermissionController
import slate.app.SlateActivity
import slate.app.health.HealthConnectWriter
import slate.app.ui.SimpleViews

/** Phone-only bridge prefs: alarm backend + Health Connect sync. */
class PhoneBridgesActivity : SlateActivity() {
    private lateinit var prefs: HostPrefs
    private lateinit var alarmStatus: TextView
    private lateinit var hcStatus: TextView

    private val requestHcPermissions = registerForActivityResult(
        PermissionController.createRequestPermissionResultContract(),
    ) {
        refresh()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = HostPrefs(this)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }

        root.addView(SimpleViews.text(this, "Phone bridges", 20f, true))
        root.addView(
            SimpleViews.text(
                this,
                "These settings stay on the phone (not synced to the watch).",
                13f,
                false,
            ),
        )

        alarmStatus = SimpleViews.text(this, "", 14f, false).also { root.addView(it) }
        root.addView(
            SimpleViews.button(this, "Alarm backend: Clock app") {
                prefs.alarmBackend = HostPrefs.BACKEND_CLOCK
                refresh()
            },
        )
        root.addView(
            SimpleViews.button(this, "Alarm backend: Exact (Slate)") {
                prefs.alarmBackend = HostPrefs.BACKEND_EXACT
                refresh()
                if (Build.VERSION.SDK_INT >= 31) {
                    startActivity(
                        Intent(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM).apply {
                            data = Uri.parse("package:$packageName")
                        },
                    )
                }
            },
        )

        hcStatus = SimpleViews.text(this, "", 14f, false).also { root.addView(it) }
        root.addView(
            SimpleViews.button(this, "Health Connect sync: On") {
                prefs.healthConnectSync = true
                refresh()
            },
        )
        root.addView(
            SimpleViews.button(this, "Health Connect sync: Off") {
                prefs.healthConnectSync = false
                refresh()
            },
        )
        root.addView(
            SimpleViews.button(this, "Grant Health Connect permissions") {
                runCatching {
                    requestHcPermissions.launch(HealthConnectWriter.ALL_PERMISSIONS)
                }
            },
        )
        root.addView(
            SimpleViews.button(this, "Open Health Connect") {
                val avail = HealthConnectWriter(this).availability()
                val intent = when (avail) {
                    "available" -> Intent("androidx.health.ACTION_HEALTH_CONNECT_SETTINGS")
                    else -> Intent(Intent.ACTION_VIEW).apply {
                        data = Uri.parse(
                            "market://details?id=com.google.android.apps.healthdata",
                        )
                    }
                }
                runCatching { startActivity(intent) }
            },
        )

        setContentView(
            android.widget.ScrollView(this).apply {
                addView(root)
            },
        )
        refresh()
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        alarmStatus.text = "Alarm backend: ${prefs.alarmBackend}"
        val hc = HealthConnectWriter(this).availability()
        hcStatus.text =
            "HC sync: ${if (prefs.healthConnectSync) "on" else "off"} ($hc)"
    }
}
