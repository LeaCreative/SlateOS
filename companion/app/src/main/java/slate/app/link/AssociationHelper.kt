package slate.app.link

import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.IntentSender
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.Parcelable
import androidx.core.content.ContextCompat
import slate.uuid.SlateUuids
import slate.ota.SealedDfuPreflight
import java.util.regex.Pattern

/**
 * CompanionDeviceManager association using the watch device profile when available.
 */
class AssociationHelper(private val context: Context) {

    enum class Target {
        /** Normal operation: only watches already running Slate. */
        Slate,
        /** First sealed install: InfiniTime/recovery exposing Nordic legacy DFU. */
        SealedInstall,
    }

    private val cdm: CompanionDeviceManager?
        get() = context.getSystemService(CompanionDeviceManager::class.java)

    fun lastAssociatedAddress(): String? =
        BtAddress.normalize(
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(KEY_LAST_ADDRESS, null),
        )

    fun rememberAddress(address: String) {
        val normalized = BtAddress.normalize(address) ?: return
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_LAST_ADDRESS, normalized)
            .apply()
        SharedLink.associatedAddress = normalized
    }

    fun presenceAddresses(): Set<String> =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getStringSet(KEY_PRESENCE_ADDRESSES, emptySet())
            ?.toSet()
            .orEmpty()

    fun associatedAddresses(): List<String> {
        val mgr = cdm ?: return emptyList()
        return if (Build.VERSION.SDK_INT >= 33) {
            mgr.myAssociations.mapNotNull { BtAddress.normalize(it.deviceMacAddress?.toString()) }
        } else {
            @Suppress("DEPRECATION")
            mgr.associations.mapNotNull { BtAddress.normalize(it) }
        }
    }

    /**
     * Records which devices CDM had associated before a chooser is launched.
     *
     * CompanionDeviceActivity creates the association itself and only then calls
     * setResult()/finish(), and CompanionDeviceManager$CallbackProxy drops its
     * callback in onActivityDestroyed. A caller that gets torn down while the
     * chooser is up therefore never sees onActivityResult even though the
     * association exists. Pairing this with [consumeNewAssociation] recovers it.
     */
    fun snapshotAssociations(tag: String) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putStringSet(pendingKey(tag), associatedAddresses().map { it.uppercase() }.toSet())
            .apply()
    }

    /**
     * The address CDM gained since [snapshotAssociations], or null if the chooser
     * was never launched or added nothing. Diffing against the snapshot works on
     * every API level, unlike AssociationInfo.getTimeApprovedMs (API 33+).
     */
    fun consumeNewAssociation(tag: String): String? {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val before = prefs.getStringSet(pendingKey(tag), null) ?: return null
        val added = associatedAddresses().map { it.uppercase() }.toSet() - before
        val picked = added.firstOrNull() ?: return null
        prefs.edit().remove(pendingKey(tag)).apply()
        LinkLog.i("recovered association $picked for '$tag' without an activity result")
        return picked
    }

    private fun pendingKey(tag: String) = "$KEY_PENDING_PREFIX$tag"

    fun startObservingPresence(address: String) {
        val observed = presenceAddresses() + address.uppercase()
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putStringSet(KEY_PRESENCE_ADDRESSES, observed)
            .apply()
        if (Build.VERSION.SDK_INT < 31) {
            LinkLog.w("startObservingDevicePresence requires API 31+")
            return
        }
        try {
            cdm?.startObservingDevicePresence(address)
            LinkLog.i("startObservingDevicePresence($address)")
        } catch (t: Throwable) {
            LinkLog.e("startObservingDevicePresence failed", t)
        }
    }

    fun buildAssociationRequest(target: Target): AssociationRequest {
        val builder = AssociationRequest.Builder()

        when (target) {
            Target.Slate -> {
                builder.addDeviceFilter(
                    BluetoothLeDeviceFilter.Builder()
                        .setScanFilter(
                            ScanFilter.Builder()
                                .setServiceUuid(ParcelUuid(SlateUuids.SERVICE))
                                .build(),
                        )
                        .build(),
                )
                builder.setSingleDevice(true)
            }
            Target.SealedInstall -> {
                // InfiniTime often advertises by name without the 128-bit DFU UUID.
                // Multiple CDM filters are alternatives, so support both signals.
                builder.addDeviceFilter(
                    BluetoothLeDeviceFilter.Builder()
                        .setNamePattern(
                            Pattern.compile(".*(InfiniTime|PineTime).*", Pattern.CASE_INSENSITIVE),
                        )
                        .setScanFilter(ScanFilter.Builder().build())
                        .build(),
                )
                builder.addDeviceFilter(
                    BluetoothLeDeviceFilter.Builder()
                        .setScanFilter(
                            ScanFilter.Builder()
                                .setServiceUuid(
                                    ParcelUuid.fromString(SealedDfuPreflight.NORDIC_DFU),
                                )
                                .build(),
                        )
                        .build(),
                )
                builder.setSingleDevice(false)
            }
        }

        // DEVICE_PROFILE_WATCH is API 31+ and requires REQUEST_COMPANION_PROFILE_WATCH.
        if (Build.VERSION.SDK_INT >= 31) {
            builder.setDeviceProfile(AssociationRequest.DEVICE_PROFILE_WATCH)
            LinkLog.i("AssociationRequest DEVICE_PROFILE_WATCH")
        } else {
            LinkLog.w("API 30 — associating without DEVICE_PROFILE_WATCH")
        }
        return builder.build()
    }

    fun associate(
        activity: Activity,
        target: Target = Target.Slate,
        onFound: (IntentSender) -> Unit,
        onFailure: (String) -> Unit,
    ) {
        if (!context.packageManager.hasSystemFeature(
                PackageManager.FEATURE_COMPANION_DEVICE_SETUP,
            )
        ) {
            onFailure("This Android device has no companion-device setup service")
            return
        }
        val mgr = cdm
        if (mgr == null) {
            onFailure("CompanionDeviceManager unavailable")
            return
        }
        val request = buildAssociationRequest(target)
        val callback = object : CompanionDeviceManager.Callback() {
            override fun onAssociationPending(intentSender: IntentSender) {
                LinkLog.i("onAssociationPending")
                onFound(intentSender)
            }

            @Deprecated("Deprecated in API 33")
            override fun onDeviceFound(intentSender: IntentSender) {
                LinkLog.i("onDeviceFound")
                onFound(intentSender)
            }

            override fun onFailure(error: CharSequence?) {
                LinkLog.w("association failure: $error")
                onFailure(error?.toString() ?: "association failed")
            }
        }
        try {
            // Main executor: launching ActivityResult from the binder thread crashes.
            if (Build.VERSION.SDK_INT >= 33) {
                mgr.associate(request, ContextCompat.getMainExecutor(activity), callback)
            } else {
                @Suppress("DEPRECATION")
                mgr.associate(request, callback, Handler(Looper.getMainLooper()))
            }
        } catch (t: SecurityException) {
            LinkLog.e("CDM associate SecurityException (check REQUEST_COMPANION_PROFILE_WATCH)", t)
            onFailure(t.message ?: "CDM permission missing")
        } catch (t: Throwable) {
            LinkLog.e("CDM associate failed", t)
            onFailure(t.message ?: "CDM associate failed")
        }
    }

    fun deviceFromAssociationResult(data: android.content.Intent?): BluetoothDevice? {
        if (data == null) return null
        return if (Build.VERSION.SDK_INT >= 33) {
            val assoc = data.getParcelableExtra(
                CompanionDeviceManager.EXTRA_ASSOCIATION,
                android.companion.AssociationInfo::class.java,
            )
            remoteDevice(assoc?.deviceMacAddress?.toString())
                ?.also { rememberAddress(it.address) }
        } else {
            @Suppress("DEPRECATION")
            val selected =
                data.getParcelableExtra<Parcelable>(CompanionDeviceManager.EXTRA_DEVICE)
            val device = when (selected) {
                is BluetoothDevice -> selected
                is ScanResult -> selected.device
                else -> null
            }
            device?.also { rememberAddress(it.address) }
        }
    }

    /**
     * Resolves an address reported by CDM into a device. Normalising first is
     * mandatory: CDM's lowercase form makes getRemoteDevice() throw.
     */
    fun remoteDevice(raw: String?): BluetoothDevice? {
        val addr = BtAddress.normalize(raw) ?: return null
        return context.getSystemService(android.bluetooth.BluetoothManager::class.java)
            ?.adapter?.getRemoteDevice(addr)
    }

    private companion object {
        const val PREFS = "companion_association"
        const val KEY_LAST_ADDRESS = "last_address"
        const val KEY_PRESENCE_ADDRESSES = "presence_addresses"
        const val KEY_PENDING_PREFIX = "pending_assoc_"
    }
}
