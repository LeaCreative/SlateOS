package slate.app.notif

import android.app.Notification
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Build
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import slate.app.link.LinkLog
import slate.notif.NotifIconMapper

/**
 * Captures posted/removed notifications for the Slate bridge.
 * Permission is *not* a runtime grant — user must enable this component in system
 * settings ([openListenerSettings]).
 */
class SlateNotificationListener : NotificationListenerService() {

    override fun onListenerConnected() {
        LinkLog.i("NLS connected")
        try {
            activeNotifications?.forEach { ingest(it, isRemoval = false) }
        } catch (t: Throwable) {
            LinkLog.e("NLS activeNotifications", t)
        }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        if (sbn == null) return
        ingest(sbn, isRemoval = false)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        if (sbn == null) return
        ingest(sbn, isRemoval = true)
    }

    private fun ingest(sbn: StatusBarNotification, isRemoval: Boolean) {
        val key = sbn.key ?: return
        if (sbn.packageName == packageName) return // ignore our own FGS notif

        if (isRemoval) {
            NotifStore.remove(key)
            return
        }

        val n = sbn.notification ?: return
        val extras = n.extras
        val title = extras?.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.trim().orEmpty()
        val text = (
            extras?.getCharSequence(Notification.EXTRA_BIG_TEXT)
                ?: extras?.getCharSequence(Notification.EXTRA_TEXT)
                ?: extras?.getCharSequence(Notification.EXTRA_SUB_TEXT)
            )?.toString()?.trim().orEmpty()

        val isSummary = (n.flags and Notification.FLAG_GROUP_SUMMARY) != 0
        // Keep summaries out of the wrist list — children carry the content.
        if (isSummary) return

        // Dedup: same key replaces; empty title+text with no actions is noise.
        if (title.isEmpty() && text.isEmpty() && n.actions.isNullOrEmpty()) return

        val importance = if (Build.VERSION.SDK_INT >= 26) {
            val ch = n.channelId
            val mgr = getSystemService(NotificationManager::class.java)
            mgr?.getNotificationChannel(ch)?.importance ?: NotificationManager.IMPORTANCE_DEFAULT
        } else {
            @Suppress("DEPRECATION")
            when (n.priority) {
                Notification.PRIORITY_MAX, Notification.PRIORITY_HIGH ->
                    NotificationManager.IMPORTANCE_HIGH
                Notification.PRIORITY_LOW, Notification.PRIORITY_MIN ->
                    NotificationManager.IMPORTANCE_LOW
                else -> NotificationManager.IMPORTANCE_DEFAULT
            }
        }

        val icon = NotifIconMapper.map(sbn.packageName, title.ifEmpty { null })
        val actions = extractActions(key, n)

        val item = NotifItem(
            key = key,
            packageName = sbn.packageName,
            title = title.ifEmpty { sbn.packageName.substringAfterLast('.') },
            text = text,
            whenMs = sbn.postTime,
            ongoing = (n.flags and Notification.FLAG_ONGOING_EVENT) != 0,
            clearable = (n.flags and Notification.FLAG_NO_CLEAR) == 0,
            importance = importance,
            isGroupSummary = false,
            icon = icon,
            actions = actions.map { it.first },
        )
        NotifStore.registerActions(key, actions.associate { it.first.id to it.second })
        NotifStore.upsert(item)
    }

    private fun extractActions(
        key: String,
        n: Notification,
    ): List<Pair<NotifAction, () -> Unit>> {
        val out = ArrayList<Pair<NotifAction, () -> Unit>>()
        val actions = n.actions ?: return out
        actions.forEachIndexed { idx, act ->
            if (act == null) return@forEachIndexed
            val title = act.title?.toString()?.trim().orEmpty()
            if (title.isEmpty()) return@forEachIndexed
            val isReply = act.remoteInputs?.isNotEmpty() == true
            val id = "a$idx"
            val pending = act.actionIntent
            out += NotifAction(id, title.take(24), isReply) to {
                try {
                    pending?.send()
                } catch (t: Throwable) {
                    LinkLog.e("NLS action $key/$id", t)
                }
            }
        }
        // Synthetic dismiss if clearable
        if ((n.flags and Notification.FLAG_NO_CLEAR) == 0) {
            out += NotifAction("dismiss", "Dismiss") to {
                try {
                    cancelNotification(key)
                } catch (t: Throwable) {
                    LinkLog.e("NLS dismiss $key", t)
                }
                NotifStore.dismissLocal(key)
            }
        }
        out += NotifAction("snooze", "Snooze") to {
            // Best-effort: cancel locally; real snooze needs OS APIs per OEM.
            try {
                cancelNotification(key)
            } catch (_: Throwable) {
            }
            NotifStore.dismissLocal(key)
        }
        return out
    }

    companion object {
        fun isEnabled(context: Context): Boolean {
            val cn = ComponentName(context, SlateNotificationListener::class.java)
            val flat = Settings.Secure.getString(
                context.contentResolver,
                "enabled_notification_listeners",
            ) ?: return false
            return flat.split(':').any {
                ComponentName.unflattenFromString(it)?.equals(cn) == true ||
                    it.contains(context.packageName) && it.contains("SlateNotificationListener")
            }
        }

        fun openListenerSettings(context: Context) {
            context.startActivity(
                Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS).addFlags(
                    Intent.FLAG_ACTIVITY_NEW_TASK,
                ),
            )
        }
    }
}
