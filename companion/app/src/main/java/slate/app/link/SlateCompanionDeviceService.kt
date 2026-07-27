package slate.app.link

import android.companion.CompanionDeviceService
import android.content.Intent
import android.os.Build
import androidx.annotation.RequiresApi

/**
 * Receives companion device presence callbacks and wakes the link FGS.
 * Registered in the manifest with BIND_COMPANION_DEVICE_SERVICE.
 */
@RequiresApi(31)
class SlateCompanionDeviceService : CompanionDeviceService() {

    @Deprecated("Deprecated in Java")
    override fun onDeviceAppeared(address: String) {
        LinkLog.i("CompanionDeviceService onDeviceAppeared $address")
        SharedLink.associatedAddress = address
        val i = Intent(this, LinkForegroundService::class.java).apply {
            action = LinkForegroundService.ACTION_PRESENCE_APPEARED
            putExtra(LinkForegroundService.EXTRA_ADDRESS, address)
        }
        if (Build.VERSION.SDK_INT >= 26) {
            startForegroundService(i)
        } else {
            startService(i)
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onDeviceDisappeared(address: String) {
        LinkLog.i("CompanionDeviceService onDeviceDisappeared $address")
        val i = Intent(this, LinkForegroundService::class.java).apply {
            action = LinkForegroundService.ACTION_PRESENCE_DISAPPEARED
            putExtra(LinkForegroundService.EXTRA_ADDRESS, address)
        }
        startService(i)
    }
}
