package slate.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.launch
import slate.app.host.CompositorHost
import slate.app.link.AssociationHelper
import slate.app.link.LinkContention
import slate.app.link.LinkForegroundService
import slate.app.link.LinkLog
import slate.app.link.LinkMetrics
import slate.app.link.SharedLink
import slate.app.notif.SlateNotificationListener
import slate.app.ota.SealedDfuProbeActivity
import slate.app.ota.SlateOtaActivity
import slate.app.repo.RepoActivity
import slate.app.ui.SimpleViews

class MainActivity : SlateActivity() {

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
            SharedLink.lastContentionMessage = held
            LinkLog.w("starting link despite contention: $held")
        }
        val started = LinkForegroundService.start(this, device.address, force = true)
        statusView.text = when {
            !started ->
                "Associated ${device.address}, but Android blocked the link service; " +
                    "check Permissions → background settings"
            held != null -> "Associated ${device.address} — connecting anyway. $held"
            else -> "Associated ${device.address} — link service started"
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }

        root.addView(
            SimpleViews.text(
                this,
                "Companion ${BuildConfig.VERSION_NAME} " +
                    "(build ${BuildConfig.VERSION_CODE}, ${BuildConfig.BUILD_TYPE})",
                12f,
                false,
            ),
        )

        metricsView = SimpleViews.text(this, "…", 15f, false).also { root.addView(it) }
        confirmBanner = SimpleViews.text(this, "", 16f, true).also {
            it.setPadding(SimpleViews.dp(this, 8), SimpleViews.dp(this, 12), SimpleViews.dp(this, 8), SimpleViews.dp(this, 12))
            it.visibility = android.view.View.GONE
            root.addView(it)
        }
        statusView = SimpleViews.text(this, "", 13f, false).also { root.addView(it) }
        nlsStatus = SimpleViews.text(this, "", 13f, false).also { root.addView(it) }

        root.addView(
            SimpleViews.button(this, "Permissions") {
                startActivity(Intent(this, PermissionsActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Associate watch (CDM)") {
                if (!hasCompanionPermissions()) {
                    statusView.text = "Open Permissions and grant BLE first"
                    return@button
                }
                val known = SharedLink.associatedAddress
                    ?: association.lastAssociatedAddress()
                    ?: association.associatedAddresses().firstOrNull()
                if (known != null) {
                    LinkContention.remediationIfHeldByOther(
                        this,
                        known,
                        weAreConnected = gatt.metrics.value.connected,
                    )?.let { LinkLog.w("associating despite contention: $it") }
                }
                association.associate(
                    activity = this,
                    onFound = { sender ->
                        associateLauncher.launch(IntentSenderRequest.Builder(sender).build())
                    },
                    onFailure = { err -> statusView.text = err },
                )
            },
        )
        root.addView(
            SimpleViews.button(this, "Start / reconnect link service") {
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
                        SharedLink.lastContentionMessage = held
                        LinkLog.w("reconnect requested despite contention: $held")
                    }
                    val started = LinkForegroundService.start(this, addr, force = true)
                    statusView.text = when {
                        !started -> "Android blocked the link service; check Permissions"
                        held != null -> "Reconnecting $addr anyway — $held"
                        else -> "Reconnecting $addr"
                    }
                }
            },
        )

        root.addView(
            SimpleViews.text(
                this,
                "JS sub-apps: swipe right-to-left on the watch for the launcher. " +
                    "Manage installs under Sub-app repository.",
                13f,
                false,
            ),
        )
        root.addView(
            SimpleViews.button(this, "Heart rate") {
                startActivity(Intent(this, slate.app.health.HeartRateActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Watch settings") {
                startActivity(Intent(this, slate.app.settings.WatchSettingsActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Phone bridges") {
                startActivity(Intent(this, slate.app.settings.PhoneBridgesActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Sub-app repository") {
                startActivity(Intent(this, RepoActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Install Slate on sealed PineTime") {
                startActivity(Intent(this, SealedDfuProbeActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Update Slate firmware (SDP OTA)") {
                startActivity(Intent(this, SlateOtaActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Debug") {
                startActivity(Intent(this, DebugActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Disconnect") {
                startService(
                    Intent(this, LinkForegroundService::class.java).apply {
                        action = LinkForegroundService.ACTION_DISCONNECT
                    },
                )
                statusView.text = "Disconnect requested"
            },
        )

        SimpleViews.setContentWithAppBar(
            this,
            title = "Slate companion",
            body = root,
        )

        val associated = association.associatedAddresses().map { it.uppercase() }.toSet()
        association.presenceAddresses()
            .filter { it.uppercase() in associated }
            .forEach(association::startObservingPresence)
        LinkForegroundService.startForRememberedWatch(this, force = true)

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
                    "disabled — open Permissions"
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
                    (m.rttMs?.let { "%.1f ms".format(it) }
                        ?: "— (Debug → Benchmarks gate B)"),
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
}
