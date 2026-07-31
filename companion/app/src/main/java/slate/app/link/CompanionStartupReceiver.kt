package slate.app.link

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build

/**
 * Presence observation is not guaranteed to survive a phone reboot or package
 * replacement. Re-register it for every persistent CDM association.
 */
class CompanionStartupReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        if (Build.VERSION.SDK_INT < 31) return
        if (intent?.action != Intent.ACTION_BOOT_COMPLETED &&
            intent?.action != Intent.ACTION_MY_PACKAGE_REPLACED
        ) {
            return
        }

        val association = AssociationHelper(context.applicationContext)
        val associated = association.associatedAddresses().map { it.uppercase() }.toSet()
        association.presenceAddresses()
            .filter { it.uppercase() in associated }
            .forEach(association::startObservingPresence)
    }
}
