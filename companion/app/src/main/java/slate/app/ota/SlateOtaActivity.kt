package slate.app.ota

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.launch
import slate.app.link.LinkContention
import slate.app.link.SharedLink

/**
 * Slate→Slate firmware update via SDP channel 5.
 *
 * Requires a running Slate watch already connected through [LinkForegroundService].
 * Accepts a [slate-dfu.zip] (same format as sealed install) and streams the
 * MCUBoot image over channel 5 with credit flow-control, SHA-256 verify, and
 * automatic IMAGE_OK confirmation on reconnect.
 *
 * Do NOT use for first-hop InfiniTime → Slate install; use [SealedDfuProbeActivity].
 */
class SlateOtaActivity : ComponentActivity() {

    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private var packageUri: Uri? = null
    private val packageSelection by lazy { DfuPackageSelection(this, PREFS) }

    private val fileLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri == null) {
            status.text = "Package selection cancelled"
            return@registerForActivityResult
        }
        packageUri = uri
        packageSelection.remember(uri)
        describePackage(uri)
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { grants ->
        status.text = if (grants.values.all { it }) {
            "Permissions granted. Select a DFU package."
        } else {
            "Required Bluetooth/notification permission denied."
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        packageUri = uriFromIntent(intent)
            ?: savedInstanceState?.getString(STATE_URI)?.let(Uri::parse)
            ?: packageSelection.restore()
        packageUri?.let {
            packageSelection.remember(it)
            // Defer until status exists — describe in onCreate after setContent.
        }

        val pad = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }

        root.addView(text("Update Slate firmware (SDP OTA)", 20f, true))
        root.addView(
            text(
                "Watch must already be connected and running Slate. Select the same " +
                    "slate-dfu.zip used for sealed install. The image is verified by SHA-256 " +
                    "before commit; the watch writes IMAGE_OK after the next reconnect.",
                13f,
                false,
            ),
        )
        val restored = packageUri?.lastPathSegment
        status = text(
            if (restored == null) {
                "Check that the watch is connected, then select a package."
            } else {
                "Package still selected: $restored. Check the watch is connected, then start."
            },
            14f,
            false,
        ).also(root::addView)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 100
        }.also(root::addView)

        root.addView(button("1. Select slate-dfu.zip") {
            fileLauncher.launch(
                arrayOf(
                    "application/zip",
                    "application/x-zip-compressed",
                    "application/octet-stream",
                ),
            )
        })
        root.addView(button("2. Start OTA update") {
            if (!hasPermissions()) {
                permissionLauncher.launch(requiredPermissions())
                return@button
            }
            val uri = packageUri
            if (uri == null) {
                status.text = "Select a DFU package first"
                return@button
            }
            val gatt = SharedLink.gatt(applicationContext)
            val addr = SharedLink.associatedAddress
                ?: gatt.metrics.value.deviceAddress.takeIf { it.isNotBlank() }
            if (addr != null) {
                val held = LinkContention.remediationIfHeldByOther(
                    this,
                    addr,
                    weAreConnected = gatt.metrics.value.connected,
                )
                if (held != null) {
                    status.text = held
                    return@button
                }
            }
            if (!gatt.metrics.value.connected) {
                status.text = "Watch is not connected — connect first via the main screen"
                return@button
            }
            SlateOtaService.start(this, uri)
        })
        root.addView(button("Cancel active OTA") { SlateOtaService.cancel(this) })
        root.addView(button("Done — main screen") { goMain() })

        setContentView(ScrollView(this).apply { addView(root) })
        packageUri?.let(::describePackage)

        var sawTransferActive = false
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                SlateOtaService.state.collect { s ->
                    if (s.active) sawTransferActive = true
                    progress.progress = s.progress
                    status.text = when {
                        s.error != null -> "${s.message}: ${s.error}"
                        s.active || s.progress > 0 -> s.message
                        packageUri != null -> describePackageText(packageUri!!)
                        else -> s.message
                    }
                    // Only leave after a transfer that ran in this session —
                    // do not bounce on a stale "complete" left in the service.
                    if (sawTransferActive && !s.active && s.progress >= 100 &&
                        s.error == null &&
                        s.message.contains("complete", ignoreCase = true)
                    ) {
                        goMain()
                    }
                }
            }
        }
    }

    private fun goMain() {
        startActivity(
            Intent(this, slate.app.MainActivity::class.java).apply {
                addFlags(
                    Intent.FLAG_ACTIVITY_CLEAR_TOP or
                        Intent.FLAG_ACTIVITY_SINGLE_TOP,
                )
            },
        )
        finish()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        val fromOpen = uriFromIntent(intent) ?: return
        packageUri = fromOpen
        packageSelection.remember(fromOpen)
        if (::status.isInitialized) {
            describePackage(fromOpen)
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString(STATE_URI, packageUri?.toString())
        super.onSaveInstanceState(outState)
    }

    private fun describePackage(uri: Uri) {
        status.text = describePackageText(uri)
    }

    private fun describePackageText(uri: Uri): String {
        val name = uri.lastPathSegment ?: uri.toString()
        return try {
            val pkg = NordicDfuPackageReader.read(contentResolver, uri)
            val stamp = NordicDfuPackageReader.faceStamp(pkg.firmware) ?: "no face stamp"
            val mcuboot = NordicDfuPackageReader.mcubootVersion(pkg.firmware) ?: "no ih_ver"
            val sha = NordicDfuPackageReader.sha12(pkg.firmware)
            val stale = if (mcuboot == "0.1.0") {
                " Note: MCUBoot header is 0.1.0 (the old hardcoded imgtool value)."
            } else {
                ""
            }
            "$name — $stamp — MCUBoot $mcuboot — ${pkg.firmware.size} B — sha $sha. " +
                "Watch must show this stamp after reboot. Then start.$stale"
        } catch (t: Throwable) {
            "$name — cannot read image: ${t.message}"
        }
    }

    private fun uriFromIntent(intent: Intent?): Uri? {
        if (intent == null) return null
        intent.getStringExtra(EXTRA_PACKAGE_URI)?.let { raw ->
            return Uri.parse(raw)
        }
        intent.data?.let { return it }
        return intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
    }

    private fun requiredPermissions(): Array<String> {
        val list = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            list += Manifest.permission.BLUETOOTH_CONNECT
        }
        if (Build.VERSION.SDK_INT >= 33) {
            list += Manifest.permission.POST_NOTIFICATIONS
        }
        return list.toTypedArray()
    }

    private fun hasPermissions() = requiredPermissions().all {
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
            if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
            setPadding(0, dp(6), 0, dp(6))
            gravity = Gravity.START
        }

    private fun button(label: String, action: () -> Unit): Button =
        Button(this).apply {
            text = label
            gravity = Gravity.START or Gravity.CENTER_VERTICAL
            setOnClickListener { action() }
        }

    companion object {
        const val EXTRA_PACKAGE_URI = "slate.ota.PACKAGE_URI"
        private const val STATE_URI = "ota_uri"
        private const val PREFS = "slate_ota_selection"
    }
}
