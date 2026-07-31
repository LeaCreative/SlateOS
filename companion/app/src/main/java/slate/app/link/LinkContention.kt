package slate.app.link

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat
import slate.link.LinkContentionLogic

/**
 * Occupancy detector for `SLATE_BLE_MAX_CONNECTIONS 1`.
 *
 * Signals (I-6 / I-15 / I-16):
 * 1. [BluetoothManager.getConnectedDevices] GATT filtered to the watch address
 *    → another app on this phone holds the link.
 * 2. Bonded + not advertising (or connect fails while bonded) → foreign central.
 *
 * All human-readable remediation copy for third-party apps lives here.
 */
object LinkContention {
    /** One-slot reality — troubleshooting and banners. */
    const val ONE_SLOT_SUMMARY: String =
        "Slate accepts only one BLE central at a time " +
            "(firmware SLATE_BLE_MAX_CONNECTIONS=1). " +
            "Any other app or phone that holds the link blocks HELLO, " +
            "IMAGE_OK confirm, OTA, and sealed DFU. " +
            "See docs/flash-sealed.md step 7."

    /**
     * App-specific remediation (single source of copy). Always lists the three
     * common blockers; [prioritizeInstalled] prefixes installed packages.
     */
    fun remediationBody(context: Context? = null): String {
        val tips = listOf(
            "Gadgetbridge → disable auto-reconnect or remove the PineTime",
            "Amazfish → disconnect the watch",
            "nRF Connect → disconnect that device tab",
        )
        if (context == null) {
            return tips.joinToString(". ", postfix = ".")
        }
        val installed = buildList {
            if (isInstalled(context, PKG_GADGETBRIDGE) ||
                isInstalled(context, PKG_GADGETBRIDGE_NIGHTLY)
            ) {
                add("Gadgetbridge is installed: disable auto-reconnect or remove the PineTime")
            }
            if (isInstalled(context, PKG_AMAZFISH)) {
                add("Amazfish is installed: disconnect the watch")
            }
            if (isInstalled(context, PKG_NRF_CONNECT)) {
                add("nRF Connect is installed: disconnect that device tab")
            }
        }
        return if (installed.isNotEmpty()) {
            installed.joinToString(". ", postfix = ". ") +
                "Also: " + tips.joinToString("; ") + "."
        } else {
            tips.joinToString(". ", postfix = ".")
        }
    }

    /** Full operator message: summary + remediation. */
    fun formatMessage(context: Context, verdict: LinkContentionLogic.Verdict): String {
        if (!verdict.blocked) return verdict.summary
        return "${verdict.summary}. ${remediationBody(context)}"
    }

    /** Legacy alias used by I-6 UI. */
    val REMEDIATION: String
        get() = remediationBody(null)

    fun remediationIfHeldByOther(
        context: Context,
        watchAddress: String,
        weAreConnected: Boolean,
    ): String? {
        val v = checkInstant(context, watchAddress, weAreConnected)
        return if (v.blocked) formatMessage(context, v) else null
    }

    /**
     * Instant check (GATT occupancy). Does not scan; use [checkWithSignals]
     * when a scan or connect-failure hint is available.
     */
    @SuppressLint("MissingPermission")
    fun checkInstant(
        context: Context,
        watchAddress: String,
        weAreConnected: Boolean,
    ): LinkContentionLogic.Verdict {
        val normalized = BtAddress.normalize(watchAddress)
            ?: return LinkContentionLogic.Verdict(
                LinkContentionLogic.Kind.Clear,
                "No watch address",
            )
        if (!hasConnectPermission(context)) {
            return LinkContentionLogic.Verdict(
                LinkContentionLogic.Kind.Clear,
                "Bluetooth permission missing",
            )
        }
        return checkWithSignals(
            context,
            normalized,
            weAreConnected,
            advertisingSeen = null,
            connectFailedWhileBonded = false,
        )
    }

    @SuppressLint("MissingPermission")
    fun checkWithSignals(
        context: Context,
        watchAddress: String,
        weAreConnected: Boolean,
        advertisingSeen: Boolean?,
        connectFailedWhileBonded: Boolean,
    ): LinkContentionLogic.Verdict {
        val normalized = BtAddress.normalize(watchAddress)
            ?: return LinkContentionLogic.Verdict(
                LinkContentionLogic.Kind.Clear,
                "No watch address",
            )
        if (!hasConnectPermission(context)) {
            return LinkContentionLogic.Verdict(
                LinkContentionLogic.Kind.Clear,
                "Bluetooth permission missing",
            )
        }
        val gatt = gattConnectedDevice(context, normalized) != null
        val bonded = isBonded(context, normalized)
        val verdict = LinkContentionLogic.evaluate(
            LinkContentionLogic.Signals(
                weAreConnected = weAreConnected,
                gattConnectedOnPhone = gatt,
                bonded = bonded,
                advertisingSeen = advertisingSeen,
                connectFailedWhileBonded = connectFailedWhileBonded,
            ),
        )
        if (verdict.blocked) {
            LinkLog.w("LinkContention: ${verdict.summary}")
            SharedLink.lastContentionMessage = formatMessage(context, verdict)
        } else if (weAreConnected) {
            SharedLink.lastContentionMessage = null
        }
        return verdict
    }

    @SuppressLint("MissingPermission")
    fun gattConnectedDevice(context: Context, normalizedAddress: String): BluetoothDevice? {
        if (!hasConnectPermission(context)) return null
        val bm = context.getSystemService(BluetoothManager::class.java) ?: return null
        return try {
            bm.getConnectedDevices(BluetoothProfile.GATT).firstOrNull {
                it.address.equals(normalizedAddress, ignoreCase = true)
            }
        } catch (_: SecurityException) {
            null
        }
    }

    @SuppressLint("MissingPermission")
    fun isBonded(context: Context, normalizedAddress: String): Boolean {
        if (!hasConnectPermission(context)) return false
        val bm = context.getSystemService(BluetoothManager::class.java) ?: return false
        val adapter = bm.adapter ?: return false
        return try {
            val device = adapter.getRemoteDevice(normalizedAddress)
            device.bondState == BluetoothDevice.BOND_BONDED
        } catch (_: Throwable) {
            false
        }
    }

    private fun isInstalled(context: Context, packageName: String): Boolean =
        try {
            context.packageManager.getPackageInfo(packageName, 0)
            true
        } catch (_: PackageManager.NameNotFoundException) {
            false
        }

    private fun hasConnectPermission(context: Context): Boolean {
        if (Build.VERSION.SDK_INT < 31) return true
        return ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.BLUETOOTH_CONNECT,
        ) == PackageManager.PERMISSION_GRANTED
    }

    const val PKG_GADGETBRIDGE = "nodomain.freeyourgadget.gadgetbridge"
    const val PKG_GADGETBRIDGE_NIGHTLY = "nodomain.freeyourgadget.gadgetbridge.nightly"
    const val PKG_AMAZFISH = "uk.co.piggz.amazfish"
    const val PKG_NRF_CONNECT = "no.nordicsemi.android.mcp"
}
