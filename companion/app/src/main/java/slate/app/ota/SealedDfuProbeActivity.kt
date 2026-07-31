package slate.app.ota

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
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
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.launch
import slate.app.link.AssociationHelper
import slate.app.link.LinkContention
import slate.app.link.SharedLink
import slate.ota.SealedDfuPreflight
import slate.ota.SealedDfuProbe
import slate.ota.SealedDfuVerdict

/**
 * Sealed first-hop installer: select InfiniTime/recovery through CDM, choose an
 * application-only adafruit-nrfutil zip, then transfer through Nordic legacy DFU.
 */
class SealedDfuProbeActivity : ComponentActivity() {
    private val association by lazy { AssociationHelper(this) }
    private val prefs by lazy { getSharedPreferences(PREFS, MODE_PRIVATE) }
    private val packageSelection by lazy { DfuPackageSelection(this, PREFS) }
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private var address: String? = null
    private var packageUri: Uri? = null

    private val associateLauncher = registerForActivityResult(
        ActivityResultContracts.StartIntentSenderForResult(),
    ) { result ->
        if (result.resultCode != RESULT_OK) {
            status.text = "Watch selection cancelled"
            return@registerForActivityResult
        }
        val device = association.deviceFromAssociationResult(result.data)
        if (device == null) {
            status.text = "No watch in CDM result"
            return@registerForActivityResult
        }
        acceptDevice(device)
    }

    private val fileLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri == null) {
            status.text = "DFU package selection cancelled"
            return@registerForActivityResult
        }
        packageUri = uri
        packageSelection.remember(uri)
        status.text = selectionSummary()
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions(),
    ) { grants ->
        status.text = if (grants.values.all { it }) {
            "Permissions granted. Select InfiniTime/recovery."
        } else {
            "Required Bluetooth/notification permission denied: $grants"
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // The CDM confirmation dialog can take this activity down and drop the user
        // back on MainActivity, so a selection that lives only in the instance (or
        // only in savedInstanceState) is lost by the time step 3 runs. Both are
        // mirrored into prefs and re-read here.
        address = savedInstanceState?.getString(STATE_ADDRESS)
            ?: prefs.getString(KEY_TARGET_ADDRESS, null)
        packageUri = savedInstanceState?.getString(STATE_URI)?.let(Uri::parse)
            ?: packageSelection.restore()
        val pad = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        root.addView(text("Install Slate on a sealed PineTime", 20f, true))
        root.addView(
            text(
                "Battery ≥30% (prefer charging). Select InfiniTime or its recovery " +
                    "firmware, then select build/dfu-prod/slate-dfu.zip. " +
                    "Enable firmware updates in InfiniTime Settings first. " +
                    "PineDFU/SoftDevice targets are blocked.",
                13f,
                false,
            ),
        )
        status = text(selectionSummary(), 14f, false).also(root::addView)
        progress = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 100
        }.also(root::addView)

        root.addView(button("1. Select InfiniTime / recovery (CDM)") {
            if (!hasPermissions()) {
                permissionLauncher.launch(requiredPermissions())
                return@button
            }
            association.snapshotAssociations(ASSOC_TAG)
            association.associate(
                activity = this,
                target = AssociationHelper.Target.SealedInstall,
                onFound = { sender ->
                    associateLauncher.launch(
                        IntentSenderRequest.Builder(sender).build(),
                    )
                },
                onFailure = { status.text = it },
            )
        })
        root.addView(button("2. Select slate-dfu.zip") {
            fileLauncher.launch(
                arrayOf(
                    "application/zip",
                    "application/x-zip-compressed",
                    "application/octet-stream",
                ),
            )
        })
        root.addView(button("3. Validate and install") {
            val selectedAddress = address
            val selectedUri = packageUri
            if (selectedAddress == null || selectedUri == null) {
                status.text = when {
                    selectedAddress == null && selectedUri == null ->
                        "Nothing selected yet — do step 1, then step 2."
                    selectedAddress == null ->
                        "No watch selected — do step 1 (the zip is still selected)."
                    else ->
                        "No slate-dfu.zip selected — do step 2 (the watch is still selected)."
                }
                return@button
            }
            val held = LinkContention.remediationIfHeldByOther(
                this,
                selectedAddress,
                weAreConnected = SharedLink.gatt(applicationContext).metrics.value.connected,
            )
            if (held != null) {
                status.text = held
                SharedLink.lastContentionMessage = held
                return@button
            }
            SealedDfuService.start(this, selectedAddress, selectedUri)
        })
        root.addView(button("Cancel active DFU") { SealedDfuService.cancel(this) })

        setContentView(ScrollView(this).apply { addView(root) })

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                SealedDfuService.state.collect { state ->
                    progress.progress = state.progress
                    status.text = if (state.error == null) {
                        state.message
                    } else {
                        "${state.message}: ${state.error}"
                    }
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
        // CDM registers the association before it returns a result, and it drops
        // its callback if this activity dies while the chooser is up — so the
        // result can never arrive even on success. Adopt whatever CDM gained.
        if (address == null) {
            association.consumeNewAssociation(ASSOC_TAG)?.let(::adoptAssociatedAddress)
        }
    }

    @SuppressLint("MissingPermission")
    private fun adoptAssociatedAddress(addr: String) {
        val device = runCatching { association.remoteDevice(addr) }.getOrNull()
        if (device != null) {
            acceptDevice(device)
            return
        }
        // Adapter off or address unusable: keep the address, skip classification.
        address = addr
        prefs.edit().putString(KEY_TARGET_ADDRESS, addr).apply()
        status.text = selectionSummary()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putString(STATE_ADDRESS, address)
        outState.putString(STATE_URI, packageUri?.toString())
        super.onSaveInstanceState(outState)
    }

    @SuppressLint("MissingPermission")
    private fun acceptDevice(device: BluetoothDevice) {
        val name = runCatching { device.name }.getOrNull()
        val verdict = SealedDfuPreflight.classify(
            SealedDfuProbe(
                advertisedName = name,
                serviceUuids = setOf(SealedDfuPreflight.NORDIC_DFU),
            ),
        )
        if (verdict == SealedDfuVerdict.BlockSoftDevice) {
            address = null
            prefs.edit().remove(KEY_TARGET_ADDRESS).apply()
            status.text = SealedDfuPreflight.userMessage(verdict)
            return
        }
        if (verdict == SealedDfuVerdict.UseSlateOta) {
            // Watch is already running Slate — redirect to the channel-5 OTA flow.
            status.text = "Watch is already running Slate. Use 'Update Slate firmware (SDP OTA)' instead."
            startActivity(Intent(this, SlateOtaActivity::class.java))
            return
        }
        address = device.address
        prefs.edit().putString(KEY_TARGET_ADDRESS, device.address).apply()
        status.text = selectionSummary()
    }

    private fun selectionSummary(): String {
        val watch = address
        val zip = packageUri
        return when {
            watch != null && zip != null ->
                "Ready: $watch + ${zip.lastPathSegment ?: "package"}. Tap 3 to install."
            watch != null -> "Watch $watch selected. Now select slate-dfu.zip (step 2)."
            zip != null ->
                "Package selected. Now select the InfiniTime/recovery watch (step 1)."
            else -> "1. Select the running InfiniTime/recovery watch."
        }
    }


    private fun requiredPermissions(): Array<String> {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            permissions += Manifest.permission.BLUETOOTH_SCAN
            permissions += Manifest.permission.BLUETOOTH_CONNECT
        }
        if (Build.VERSION.SDK_INT >= 33) {
            permissions += Manifest.permission.POST_NOTIFICATIONS
        }
        return permissions.toTypedArray()
    }

    private fun hasPermissions(): Boolean = requiredPermissions().all {
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

    private companion object {
        const val STATE_ADDRESS = "sealed_address"
        const val STATE_URI = "sealed_uri"
        const val PREFS = "sealed_dfu_selection"
        // Kept apart from AssociationHelper's last-associated address: that one is
        // whatever CDM saw last, which may be a Slate or blocked target. This key
        // only ever holds an address the preflight cleared for a first-hop install.
        const val KEY_TARGET_ADDRESS = "target_address"
        const val ASSOC_TAG = "sealed_install"
    }
}
