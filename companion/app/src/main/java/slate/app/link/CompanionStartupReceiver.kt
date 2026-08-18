package slate.app.link

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build

/**
 * Presence observation is not guaranteed to survive a phone reboot or package
 * replacement. Re-register it and start the link FGS for every persistent
 * CDM association — do not wait for the operator to tap Reconnect.
 */
class CompanionStartupReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        val action = intent?.action ?: return
        if (Build.VERSION.SDK_INT < 31) return
        if (action != Intent.ACTION_BOOT_COMPLETED &&
            action != Intent.ACTION_MY_PACKAGE_REPLACED
        ) {
            return
        }

        val app = context.applicationContext
        val association = AssociationHelper(app)
        val associated = association.associatedAddresses().map { it.uppercase() }.toSet()
        association.presenceAddresses()
            .filter { it.uppercase() in associated }
            .forEach(association::startObservingPresence)
        val autoConnect = action == Intent.ACTION_BOOT_COMPLETED
        val started = LinkForegroundService.startForRememberedWatch(
            app,
            force = false,
            autoConnect = autoConnect,
        )
        LinkLog.i(
            "$action — presence re-registered" +
                if (started) ", link service started" else ", no remembered watch / no BT perm",
        )
    }
}
