package slate.app.alarms

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.core.app.NotificationCompat
import slate.app.MainActivity
import slate.app.link.LinkLog

/** Fires Slate-owned exact alarms as a high-priority notification. */
class ExactAlarmReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        if (intent?.action != ACTION) return
        val label = intent.getStringExtra(EXTRA_LABEL)?.ifBlank { "Slate alarm" } ?: "Slate alarm"
        val id = intent.getStringExtra(EXTRA_ID) ?: "alarm"
        LinkLog.i("exact alarm fired id=$id")
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        if (Build.VERSION.SDK_INT >= 26) {
            nm.createNotificationChannel(
                NotificationChannel(
                    CHANNEL,
                    "Slate alarms",
                    NotificationManager.IMPORTANCE_HIGH,
                ),
            )
        }
        val open = PendingIntent.getActivity(
            context,
            0,
            Intent(context, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val notif = NotificationCompat.Builder(context, CHANNEL)
            .setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
            .setContentTitle(label)
            .setContentText("Alarm from watch")
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
            .setContentIntent(open)
            .build()
        nm.notify(id.hashCode(), notif)
    }

    companion object {
        const val ACTION = "slate.app.action.EXACT_ALARM"
        const val EXTRA_ID = "id"
        const val EXTRA_LABEL = "label"
        private const val CHANNEL = "slate_exact_alarms"
    }
}
