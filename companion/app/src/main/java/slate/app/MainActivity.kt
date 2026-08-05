package slate.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.util.TypedValue
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.core.view.setPadding
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.launch
import slate.app.bench.BenchmarkActivity
import slate.app.host.CompositorHost
import slate.app.link.AssociationHelper
import slate.app.link.LinkContention
import slate.app.link.LinkForegroundService
import slate.app.link.LinkLog
import slate.app.link.LinkMetrics
import slate.app.link.SharedLink
import slate.app.notif.NotifPrefs
import slate.app.notif.SlateNotificationListener
import slate.app.ota.SealedDfuProbeActivity
import slate.app.ota.SlateOtaActivity
import slate.app.repo.RepoActivity
import slate.app.script.DevConsoleActivity

class MainActivity : ComponentActivity() {

    private lateinit var metricsView: TextView
    private lateinit var confirmBanner: TextView
    private lateinit var statusView: TextView
    private lateinit var nlsStatus: TextView
    private val association by lazy { AssociationHelper(this) }
    private val gatt by lazy { SharedLink.gatt(applicationContext) }

    private val associateLauncher = registerForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult(),
    ) { result ->
        if (result.resultCode != RESULT_OK) {
            statusView.text = "Association cancelled"
            return@registerForActivityResult
        }
        val device = association.deviceFromAssociationResult(result.data)
        if (device == null) {
            statusView.text = "No device in association result"
            return@registerForActivityResult
        }
        LinkLog.i("Associated ${device.address}")
        association.startObservingPresence(device.address)
        val held = LinkContention.remediationIfHeldByOther(
            this,
            device.address,
            weAreConnected = gatt.metrics.value.connected,
        )
        if (held != null) {
            statusView.text = held
            SharedLink.lastContentionMessage = held
            return@registerForActivityResult
        }
        statusView.text = if (LinkForegroundService.start(this, device.address)) {
            "Associated ${device.address} — link service started"
        } else {
            "Associated ${device.address}, but Android blocked the link service; " +
                "check notification/background settings"
        }
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { granted ->
        statusView.text = if (granted.values.all { it }) {
            "Permissions granted — tap Associate"
        } else {
            "Missing permissions: $granted"
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val pad = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad)
        }

        root.addView(text("Slate companion (M16)", 22f, true))
        root.addView(
            text(
                "CDM · FGS · compositor · JS sub-apps (nav / camera / timer) · notifications",
                14f,
                false,
            ),
        )

        // Which build is on the phone — needed constantly when firmware and
        // companion are being flashed in lockstep.
        root.addView(
            text(
                "Companion ${slate.app.BuildConfig.VERSION_NAME} " +
                    "(build ${slate.app.BuildConfig.VERSION_CODE}, " +
                    "${slate.app.BuildConfig.BUILD_TYPE})",
                12f,
                false,
            ),
        )

        metricsView = text("…", 15f, false).also { root.addView(it) }
        confirmBanner = text("", 16f, true).also {
            it.setPadding(dp(8), dp(12), dp(8), dp(12))
            it.visibility = android.view.View.GONE
            root.addView(it)
        }
        statusView = text("", 13f, false).also { root.addView(it) }
        root.addView(
            text(
                "NLS: " + if (SlateNotificationListener.isEnabled(this)) "enabled" else "disabled — open settings",
                13f,
                false,
            ).also { nlsStatus = it },
        )

        root.addView(button("1. Grant permissions") {
            permissionLauncher.launch(companionPermissions())
        })
        root.addView(button("1b. Notification access (system settings)") {
            SlateNotificationListener.openListenerSettings(this)
            statusView.text = "Enable “Slate notifications” in the list, then return"
        })
        root.addView(button("2. Associate watch (CDM)") {
            if (!hasCompanionPermissions()) {
                statusView.text = "Grant permissions first"
                return@button
            }
            val known = SharedLink.associatedAddress
                ?: association.lastAssociatedAddress()
                ?: association.associatedAddresses().firstOrNull()
            if (known != null) {
                val held = LinkContention.remediationIfHeldByOther(
                    this,
                    known,
                    weAreConnected = gatt.metrics.value.connected,
                )
                if (held != null) {
                    statusView.text = held
                    return@button
                }
            }
            association.associate(
                activity = this,
                onFound = { sender ->
                    associateLauncher.launch(IntentSenderRequest.Builder(sender).build())
                },
                onFailure = { err -> statusView.text = err },
            )
        })
        root.addView(button("3. Start / reconnect link service") {
            val addr = SharedLink.associatedAddress
                ?: association.lastAssociatedAddress()
                ?: association.associatedAddresses().firstOrNull()
            if (addr == null) {
                statusView.text = "No associated device"
            } else {
                association.startObservingPresence(addr)
                val held = LinkContention.remediationIfHeldByOther(
                    this,
                    addr,
                    weAreConnected = gatt.metrics.value.connected,
                )
                if (held != null) {
                    statusView.text = held
                    SharedLink.lastContentionMessage = held
                    return@button
                }
                statusView.text = if (LinkForegroundService.start(this, addr)) {
                    "Reconnecting $addr"
                } else {
                    "Android blocked the link service; check background settings"
                }
            }
        })
        root.addView(button("Ping RTT (DIAG ch7)") { gatt.pingRtt() })
        root.addView(button("Open TestApp") {
            LinkForegroundService.openTestApp(this)
            statusView.text = "Requested TestApp focus"
        })
        root.addView(button("Open Notifications") {
            LinkForegroundService.openNotifications(this)
            statusView.text = "Requested Notifications focus"
        })
        root.addView(button("Open Timer (JS sub-app)") {
            LinkForegroundService.openTimer(this)
            statusView.text = "Requested Timer JS focus"
        })
        root.addView(button("Open Navigation (JS)") {
            LinkForegroundService.openNavigation(this)
            statusView.text = "Requested Navigation JS focus (one DL / maneuver)"
        })
        root.addView(button("Open Camera (JS + PATCH)") {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA)
                != PackageManager.PERMISSION_GRANTED
            ) {
                permissionLauncher.launch(arrayOf(Manifest.permission.CAMERA))
                statusView.text = "Grant camera, then tap Open Camera again"
                return@button
            }
            LinkForegroundService.openCamera(this)
            statusView.text = "Requested Camera JS focus (RGB332 PATCH stream)"
        })
        root.addView(button("Script console") {
            startActivity(Intent(this, DevConsoleActivity::class.java))
        })
        root.addView(button("Sub-app repository") {
            startActivity(Intent(this, RepoActivity::class.java))
        })
        root.addView(button("Interrupt filter: allow this phone's SMS pkgs") {
            val prefs = NotifPrefs(this)
            prefs.interruptFilterEnabled = true
            prefs.allowInterrupt("com.google.android.apps.messaging")
            prefs.allowInterrupt("com.android.mms")
            statusView.text = "Interrupt allowlist: messaging (filter ON)"
        })
        root.addView(button("Interrupt filter: allow all HIGH+") {
            NotifPrefs(this).interruptFilterEnabled = false
            statusView.text = "Interrupt filter OFF — any HIGH+ may steal focus"
        })
        root.addView(button("Install Slate on sealed PineTime") {
            startActivity(Intent(this, SealedDfuProbeActivity::class.java))
        })
        root.addView(button("Update Slate firmware (SDP OTA)") {
            startActivity(Intent(this, SlateOtaActivity::class.java))
        })
        root.addView(button("Troubleshooting (BLE one-slot)") {
            startActivity(Intent(this, slate.app.link.TroubleshootingActivity::class.java))
        })
        root.addView(button("View log") {
            startActivity(Intent(this, slate.app.link.LogActivity::class.java))
        })
        root.addView(button("Background reliability settings") {
            startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
            statusView.text =
                "Allow Slate to run unrestricted if your phone kills the link service"
        })
        root.addView(button("Slate app battery / autostart settings") {
            startActivity(
                Intent(
                    Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    Uri.fromParts("package", packageName, null),
                ),
            )
            statusView.text = manufacturerBackgroundGuidance()
        })
        root.addView(button("Benchmarks (gates A / B / D)") {
            startActivity(Intent(this, BenchmarkActivity::class.java))
        })
        root.addView(button("Disconnect") {
            startService(
                Intent(this, LinkForegroundService::class.java).apply {
                    action = LinkForegroundService.ACTION_DISCONNECT
                },
            )
            statusView.text = "Disconnect requested"
        })

        setContentView(ScrollView(this).apply { addView(root) })

        // Associations persist, but observation may not survive reboot/update.
        val associated = association.associatedAddresses().map { it.uppercase() }.toSet()
        association.presenceAddresses()
            .filter { it.uppercase() in associated }
            .forEach(association::startObservingPresence)
        showBatteryOptimizationWarning()

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                gatt.metrics.collect { renderMetrics(it) }
            }
        }
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                SharedLink.confirmUi.collect { renderConfirmBanner(it) }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (::nlsStatus.isInitialized) {
            nlsStatus.text =
                "NLS: " + if (SlateNotificationListener.isEnabled(this)) {
                    "enabled"
                } else {
                    "disabled — open settings"
                }
        }
        SharedLink.lastContentionMessage?.let {
            if (::statusView.isInitialized) statusView.text = it
        }
    }

    private fun renderConfirmBanner(ui: CompositorHost.ConfirmUi) {
        if (!::confirmBanner.isInitialized) return
        when (ui) {
            is CompositorHost.ConfirmUi.Idle -> {
                confirmBanner.visibility = android.view.View.GONE
            }
            is CompositorHost.ConfirmUi.OnTrial -> {
                confirmBanner.visibility = android.view.View.VISIBLE
                confirmBanner.setBackgroundColor(0xFFFFCC00.toInt())
                confirmBanner.setTextColor(0xFF000000.toInt())
                confirmBanner.text =
                    "Image on trial — keep connected, ${ui.secondsRemaining} s to confirm"
            }
            is CompositorHost.ConfirmUi.Confirmed -> {
                confirmBanner.visibility = android.view.View.VISIBLE
                confirmBanner.setBackgroundColor(0xFF2E7D32.toInt())
                confirmBanner.setTextColor(0xFFFFFFFF.toInt())
                confirmBanner.text = "Confirmed — image is permanent"
            }
            is CompositorHost.ConfirmUi.StuckWarning -> {
                confirmBanner.visibility = android.view.View.VISIBLE
                confirmBanner.setBackgroundColor(0xFFC62828.toInt())
                confirmBanner.setTextColor(0xFFFFFFFF.toInt())
                confirmBanner.text =
                    "Trial not clearing after HELLO. " +
                        (SharedLink.lastContentionMessage
                            ?: LinkContention.remediationBody(this))
            }
        }
    }

    private fun renderMetrics(m: LinkMetrics) {
        metricsView.text = buildString {
            appendLine("Negotiated link")
            appendLine("Connected: ${m.connected}")
            appendLine("Address: ${m.deviceAddress.ifBlank { "—" }}")
            appendLine("ATT MTU: ${m.attMtu} (target 247)")
            appendLine("PHY TX/RX: ${m.phyTx} / ${m.phyRx} (target 2M)")
            appendLine(
                "Conn interval: " +
                    (m.intervalMs?.let { "%.2f ms".format(it) }
                        ?: "— (from STATUS when available)"),
            )
            appendLine(
                "RTT: " +
                    (m.rttMs?.let { "%.1f ms".format(it) } ?: "— (DIAG loopback)"),
            )
            appendLine("Notes: ${m.notes.ifBlank { "—" }}")
            if (m.lastError.isNotBlank()) appendLine("Error: ${m.lastError}")
            SharedLink.lastContentionMessage?.let { appendLine("Link: $it") }
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

    private fun hasCompanionPermissions(): Boolean =
        companionPermissions().all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }

    private fun showBatteryOptimizationWarning() {
        val pm = getSystemService(PowerManager::class.java) ?: return
        if (!pm.isIgnoringBatteryOptimizations(packageName)) {
            statusView.text =
                manufacturerBackgroundGuidance()
        }
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

    private fun dp(v: Int): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP,
            v.toFloat(),
            resources.displayMetrics,
        ).toInt()

    private fun text(value: String, sp: Float, bold: Boolean): TextView =
        TextView(this).apply {
            text = value
            setTextSize(TypedValue.COMPLEX_UNIT_SP, sp)
            if (bold) typeface = Typeface.DEFAULT_BOLD
            setPadding(0, dp(6), 0, dp(6))
        }

    private fun button(label: String, onClick: () -> Unit): Button =
        Button(this).apply {
            text = label
            gravity = Gravity.START or Gravity.CENTER_VERTICAL
            setOnClickListener { onClick() }
        }
}
