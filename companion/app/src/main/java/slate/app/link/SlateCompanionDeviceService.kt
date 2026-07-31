package slate.app.link

import android.companion.CompanionDeviceService
import android.companion.AssociationInfo
import android.content.Intent
import androidx.annotation.RequiresApi
import androidx.core.content.ContextCompat

/**
 * Receives companion device presence callbacks and wakes the link FGS.
 * Registered in the manifest with BIND_COMPANION_DEVICE_SERVICE.
 */
@RequiresApi(31)
class SlateCompanionDeviceService : CompanionDeviceService() {

    @RequiresApi(33)
    override fun onDeviceAppeared(associationInfo: AssociationInfo) {
        associationInfo.deviceMacAddress?.toString()?.let(::handleAppeared)
    }

    @RequiresApi(33)
    override fun onDeviceDisappeared(associationInfo: AssociationInfo) {
        associationInfo.deviceMacAddress?.toString()?.let(::handleDisappeared)
    }

    @Deprecated("Deprecated in Java")
    override fun onDeviceAppeared(address: String) {
        handleAppeared(address)
    }

    @Deprecated("Deprecated in Java")
    override fun onDeviceDisappeared(address: String) {
        handleDisappeared(address)
    }

    private fun handleAppeared(raw: String) {
        val address = BtAddress.normalize(raw) ?: return
        LinkLog.i("CompanionDeviceService onDeviceAppeared $address")
        SharedLink.associatedAddress = address
        val i = Intent(this, LinkForegroundService::class.java).apply {
            action = LinkForegroundService.ACTION_PRESENCE_APPEARED
            putExtra(LinkForegroundService.EXTRA_ADDRESS, address)
        }
        ContextCompat.startForegroundService(this, i)
    }

    private fun handleDisappeared(raw: String) {
        val address = BtAddress.normalize(raw) ?: return
        LinkLog.i("CompanionDeviceService onDeviceDisappeared $address")
        // Do not create an idle foreground service merely because a watch left
        // range. If the link service is running, tell that existing instance.
        if (LinkForegroundService.instance == null) return
        val i = Intent(this, LinkForegroundService::class.java).apply {
            action = LinkForegroundService.ACTION_PRESENCE_DISAPPEARED
            putExtra(LinkForegroundService.EXTRA_ADDRESS, address)
        }
        startService(i)
    }
}
