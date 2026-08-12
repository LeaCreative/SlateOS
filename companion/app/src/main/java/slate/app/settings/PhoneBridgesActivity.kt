package slate.app.settings

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.health.connect.client.PermissionController
import com.google.android.material.button.MaterialButton
import slate.app.SlateActivity
import slate.app.health.HealthConnectWriter
import slate.app.link.LinkLog
import slate.app.ui.SimpleViews

/** Phone-only bridge prefs: alarm backend + Health Connect sync. */
class PhoneBridgesActivity : SlateActivity() {
    private lateinit var prefs: HostPrefs
    private lateinit var alarmStatus: TextView
    private lateinit var hcStatus: TextView
    private lateinit var hcSyncButton: MaterialButton

    private val requestHcPermissions = registerForActivityResult(
        PermissionController.createRequestPermissionResultContract(),
    ) { granted ->
        LinkLog.i("HC grant result size=${granted.size}")
        val need = HealthConnectWriter.ALL_PERMISSIONS
        val ok = granted.containsAll(need)
        Toast.makeText(
            this,
            when {
                ok -> "Health Connect permissions granted"
                granted.isEmpty() ->
                    "HC returned no grants — open Health Connect and enable Slate"
                else -> "Partial HC grant (${granted.size}/${need.size})"
            },
            Toast.LENGTH_LONG,
        ).show()
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
        // One toggle — previously two On/Off buttons for the same boolean.
        hcSyncButton = SimpleViews.button(this, "") {
            prefs.healthConnectSync = !prefs.healthConnectSync
            refresh()
        }.also { root.addView(it) }
        root.addView(
            SimpleViews.button(this, "Grant Health Connect permissions") {
                val avail = HealthConnectWriter(this).availability()
                if (avail != "available") {
                    Toast.makeText(
                        this,
                        "Health Connect not available ($avail)",
                        Toast.LENGTH_LONG,
                    ).show()
                    return@button
                }
                try {
                    requestHcPermissions.launch(HealthConnectWriter.ALL_PERMISSIONS)
                } catch (t: Throwable) {
                    LinkLog.w("HC grant launch failed: ${t.message}")
                    Toast.makeText(
                        this,
                        "Could not open HC permissions: ${t.message}",
                        Toast.LENGTH_LONG,
                    ).show()
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
                    .onFailure { t ->
                        LinkLog.w("Open HC failed: ${t.message}")
                        Toast.makeText(this, "Could not open Health Connect", Toast.LENGTH_LONG)
                            .show()
                    }
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
        val syncOn = prefs.healthConnectSync
        hcStatus.text = "HC sync: ${if (syncOn) "on" else "off"} ($hc)"
        hcSyncButton.text =
            if (syncOn) "Health Connect sync: On (tap to turn off)"
            else "Health Connect sync: Off (tap to turn on)"
    }
}
