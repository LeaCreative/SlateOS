package slate.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import slate.app.link.LinkForegroundService
import slate.app.notif.SlateNotificationListener
import slate.app.ui.SimpleViews

/**
 * Runtime and system grants the companion needs. Kept off the main screen so
 * day-to-day link controls stay short.
 */
class PermissionsActivity : SlateActivity() {

    private lateinit var statusView: TextView
    private lateinit var nlsStatus: TextView

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        statusView.text = if (granted.values.all { it }) {
            "Permissions granted"
        } else {
            "Missing permissions: $granted"
        }
    }

    /**
     * Location is requested separately because the link service’s foreground
     * type is fixed at start — a later grant needs
     * [LinkForegroundService.refreshForegroundServiceType].
     */
    private val locationLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        if (LinkForegroundService.hasLocationPermission(this)) {
            LinkForegroundService.refreshForegroundServiceType(this)
        }
        val precise = granted[Manifest.permission.ACCESS_FINE_LOCATION] == true
        val coarse = granted[Manifest.permission.ACCESS_COARSE_LOCATION] == true
        val camera = granted[Manifest.permission.CAMERA]
        val parts = mutableListOf<String>()
        when {
            precise -> parts += "location: precise"
            coarse -> parts += "location: approximate only (low accuracy fixes)"
            granted.containsKey(Manifest.permission.ACCESS_COARSE_LOCATION) ->
                parts += "location: refused — sub-apps will be told 'denied'"
        }
        if (camera == true) parts += "camera: granted"
        if (camera == false) parts += "camera: refused"
        statusView.text = parts.joinToString(" · ").ifBlank { "Nothing changed" }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }

        root.addView(
            SimpleViews.text(
                this,
                "BLE, notifications, sub-app grants, and OEM background settings.",
                14f,
                false,
            ),
        )
        statusView = SimpleViews.text(this, "", 13f, false).also { root.addView(it) }
        nlsStatus = SimpleViews.text(this, "", 13f, false).also { root.addView(it) }

        root.addView(
            SimpleViews.button(this, "Grant BLE / notification permissions") {
                permissionLauncher.launch(companionPermissions())
            },
        )
        root.addView(
            SimpleViews.button(this, "Grant sub-app permissions (location, camera)") {
                val wanted = mutableListOf<String>()
                if (!LinkForegroundService.hasLocationPermission(this)) {
                    wanted += Manifest.permission.ACCESS_COARSE_LOCATION
                    wanted += Manifest.permission.ACCESS_FINE_LOCATION
                }
                if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                    != PackageManager.PERMISSION_GRANTED
                ) {
                    wanted += Manifest.permission.CAMERA
                }
                if (wanted.isEmpty()) {
                    LinkForegroundService.refreshForegroundServiceType(this)
                    statusView.text = "Already granted — foreground service type refreshed"
                    return@button
                }
                locationLauncher.launch(wanted.toTypedArray())
            },
        )
        root.addView(
            SimpleViews.button(this, "Notification access (system settings)") {
                SlateNotificationListener.openListenerSettings(this)
                statusView.text = "Enable “Slate notifications” in the list, then return"
            },
        )
        root.addView(
            SimpleViews.button(this, "Background reliability settings") {
                startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
                statusView.text =
                    "Allow Slate to run unrestricted if your phone kills the link service"
            },
        )
        root.addView(
            SimpleViews.button(this, "Slate app battery / autostart settings") {
                startActivity(
                    Intent(
                        Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        Uri.fromParts("package", packageName, null),
                    ),
                )
                statusView.text = manufacturerBackgroundGuidance()
            },
        )

        SimpleViews.setContentWithAppBar(
            this,
            title = "Permissions",
            body = root,
        )
        if (!isIgnoringBatteryOptimizations()) {
            statusView.text = manufacturerBackgroundGuidance()
        }
    }

    override fun onResume() {
        super.onResume()
        if (::nlsStatus.isInitialized) {
            nlsStatus.text =
                "NLS: " + if (SlateNotificationListener.isEnabled(this)) {
                    "enabled"
                } else {
                    "disabled — open Notification access"
                }
        }
    }

    private fun companionPermissions(): Array<String> {
        val list = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            list += Manifest.permission.BLUETOOTH_CONNECT
            list += Manifest.permission.BLUETOOTH_SCAN
        }
        if (Build.VERSION.SDK_INT >= 33) {
            list += Manifest.permission.POST_NOTIFICATIONS
        }
        return list.toTypedArray()
    }

    private fun isIgnoringBatteryOptimizations(): Boolean {
        val pm = getSystemService(PowerManager::class.java) ?: return true
        return pm.isIgnoringBatteryOptimizations(packageName)
    }

    private fun manufacturerBackgroundGuidance(): String {
        val steps = when {
            Build.MANUFACTURER.contains("xiaomi", ignoreCase = true) ->
                "enable Autostart and set Battery saver to No restrictions"
            Build.MANUFACTURER.contains("samsung", ignoreCase = true) ->
                "add Slate to Battery > Background usage limits > Never sleeping apps"
            Build.MANUFACTURER.contains("huawei", ignoreCase = true) ||
                Build.MANUFACTURER.contains("honor", ignoreCase = true) ->
                "open App launch, manage Slate manually, and enable all background toggles"
            else ->
                "set battery/background usage to Unrestricted and allow autostart if offered"
        }
        return "Background restrictions can stop the BLE link: $steps. " +
            "Also open “Background reliability settings” and allow Slate."
    }
}
