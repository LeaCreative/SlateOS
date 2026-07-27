package slate.app.link

import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.bluetooth.le.ScanFilter
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.IntentSender
import android.os.Build
import android.os.ParcelUuid
import slate.uuid.SlateUuids

/**
 * CompanionDeviceManager association using the watch device profile when available.
 */
class AssociationHelper(private val context: Context) {

    private val cdm: CompanionDeviceManager?
        get() = context.getSystemService(CompanionDeviceManager::class.java)

    fun associatedAddresses(): List<String> {
        val mgr = cdm ?: return emptyList()
        return if (Build.VERSION.SDK_INT >= 33) {
            mgr.myAssociations.map { it.deviceMacAddress?.toString() ?: "" }.filter { it.isNotEmpty() }
        } else {
            @Suppress("DEPRECATION")
            mgr.associations
        }
    }

    fun startObservingPresence(address: String) {
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

    fun buildAssociationRequest(): AssociationRequest {
        val filter = BluetoothLeDeviceFilter.Builder()
            .setScanFilter(
                ScanFilter.Builder()
                    .setServiceUuid(ParcelUuid(SlateUuids.SERVICE))
                    .build(),
            )
            .build()

        val builder = AssociationRequest.Builder()
            .addDeviceFilter(filter)
            .setSingleDevice(true)

        // DEVICE_PROFILE_WATCH is API 33+.
        if (Build.VERSION.SDK_INT >= 33) {
            builder.setDeviceProfile(AssociationRequest.DEVICE_PROFILE_WATCH)
            LinkLog.i("AssociationRequest DEVICE_PROFILE_WATCH")
        } else {
            LinkLog.w("API < 33 — associating without DEVICE_PROFILE_WATCH")
        }
        return builder.build()
    }

    fun associate(
        activity: Activity,
        onFound: (IntentSender) -> Unit,
        onFailure: (String) -> Unit,
    ) {
        val mgr = cdm
        if (mgr == null) {
            onFailure("CompanionDeviceManager unavailable")
            return
        }
        val request = buildAssociationRequest()
        mgr.associate(
            request,
            object : CompanionDeviceManager.Callback() {
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
            },
            null,
        )
    }

    fun deviceFromAssociationResult(data: android.content.Intent?): BluetoothDevice? {
        if (data == null) return null
        return if (Build.VERSION.SDK_INT >= 33) {
            val assoc = data.getParcelableExtra(
                CompanionDeviceManager.EXTRA_ASSOCIATION,
                android.companion.AssociationInfo::class.java,
            )
            val addr = assoc?.deviceMacAddress?.toString()
            if (addr != null) {
                SharedLink.associatedAddress = addr
                context.getSystemService(android.bluetooth.BluetoothManager::class.java)
                    ?.adapter?.getRemoteDevice(addr)
            } else {
                null
            }
        } else {
            @Suppress("DEPRECATION")
            data.getParcelableExtra<BluetoothDevice>(CompanionDeviceManager.EXTRA_DEVICE)
                ?.also { SharedLink.associatedAddress = it.address }
        }
    }
}
