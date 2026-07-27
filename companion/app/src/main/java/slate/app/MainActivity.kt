package slate.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
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
import slate.app.link.AssociationHelper
import slate.app.link.LinkForegroundService
import slate.app.link.LinkLog
import slate.app.link.LinkMetrics
import slate.app.link.SharedLink

class MainActivity : ComponentActivity() {

    private lateinit var metricsView: TextView
    private lateinit var statusView: TextView
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
        LinkForegroundService.start(this, device.address)
        statusView.text = "Associated ${device.address} — link service started"
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

        root.addView(text("Slate companion (M6)", 22f, true))
        root.addView(
            text(
                "CDM watch profile (API 33+) · connectedDevice FGS · raw BluetoothGatt · " +
                    "M4 DSL clock @ 1 Hz · §4.2 framing",
                14f,
                false,
            ),
        )

        metricsView = text("…", 15f, false).also { root.addView(it) }
        statusView = text("", 13f, false).also { root.addView(it) }

        root.addView(button("1. Grant permissions") {
            permissionLauncher.launch(requiredPermissions())
        })
        root.addView(button("2. Associate watch (CDM)") {
            if (!hasPermissions()) {
                statusView.text = "Grant permissions first"
                return@button
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
                ?: association.associatedAddresses().firstOrNull()
            if (addr == null) {
                statusView.text = "No associated device"
            } else {
                association.startObservingPresence(addr)
                LinkForegroundService.start(this, addr)
                statusView.text = "Reconnecting $addr"
            }
        })
        root.addView(button("Ping RTT (DIAG ch7)") { gatt.pingRtt() })
        root.addView(button("Open TestApp (compositor)") {
            LinkForegroundService.openTestApp(this)
            statusView.text = "Requested TestApp focus"
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

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                gatt.metrics.collect { renderMetrics(it) }
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
        }
    }

    private fun requiredPermissions(): Array<String> {
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

    private fun hasPermissions(): Boolean =
        requiredPermissions().all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
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
