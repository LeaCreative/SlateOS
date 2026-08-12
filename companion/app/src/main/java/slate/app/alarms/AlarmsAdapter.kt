package slate.app.alarms

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.provider.AlarmClock
import org.json.JSONArray
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.app.settings.HostPrefs
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * Phone alarms from the watch UI.
 *
 * Backend is [HostPrefs.alarmBackend]: `clock` opens the system Clock app;
 * `exact` uses Slate-owned [AlarmManager] one-shots.
 */
class AlarmsAdapter(
    private val context: Context,
    private val onEvent: (json: String) -> Unit,
) {
    private val prefs = HostPrefs(context)
    private val exact = ConcurrentHashMap<String, ExactAlarm>()

    data class ExactAlarm(val id: String, val whenMs: Long, val label: String)

    fun set(whenMs: Long, label: String, idIn: String) {
        if (whenMs < System.currentTimeMillis() + 5_000L) {
            emitStatus("error", "whenMs too soon")
            return
        }
        val id = idIn.ifBlank { UUID.randomUUID().toString().take(12) }
        val backend = prefs.alarmBackend
        when (backend) {
            HostPrefs.BACKEND_EXACT -> setExact(id, whenMs, label.ifBlank { "Slate" })
            else -> setClock(whenMs, label.ifBlank { "Slate" }, id)
        }
    }

    fun cancel(id: String) {
        if (id.isBlank()) return
        val am = context.getSystemService(AlarmManager::class.java) ?: return
        am.cancel(pendingFor(id, exact[id]?.label ?: "Slate"))
        exact.remove(id)
        list()
    }

    fun list() {
        val arr = JSONArray()
        for (a in exact.values.sortedBy { it.whenMs }) {
            arr.put(
                JSONObject()
                    .put("id", a.id)
                    .put("whenMs", a.whenMs)
                    .put("label", a.label)
                    .put("backend", HostPrefs.BACKEND_EXACT),
            )
        }
        emit(JSONObject().put("type", "list").put("items", arr))
    }

    fun stop() {
        // Exact alarms stay scheduled; only clear JS subscription side-effects.
    }

    private fun setClock(whenMs: Long, label: String, id: String) {
        val cal = java.util.Calendar.getInstance().apply { timeInMillis = whenMs }
        val intent = Intent(AlarmClock.ACTION_SET_ALARM).apply {
            putExtra(AlarmClock.EXTRA_HOUR, cal.get(java.util.Calendar.HOUR_OF_DAY))
            putExtra(AlarmClock.EXTRA_MINUTES, cal.get(java.util.Calendar.MINUTE))
            putExtra(AlarmClock.EXTRA_MESSAGE, label.take(32))
            putExtra(AlarmClock.EXTRA_SKIP_UI, false)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        try {
            context.startActivity(intent)
            emit(
                JSONObject()
                    .put("type", "scheduled")
                    .put("id", id)
                    .put("whenMs", whenMs)
                    .put("backend", HostPrefs.BACKEND_CLOCK),
            )
        } catch (t: Throwable) {
            LinkLog.w("alarms.clock failed: ${t.message}")
            emitStatus("error", t.message ?: "clock")
        }
    }

    private fun setExact(id: String, whenMs: Long, label: String) {
        val am = context.getSystemService(AlarmManager::class.java) ?: run {
            emitStatus("error", "no AlarmManager")
            return
        }
        if (Build.VERSION.SDK_INT >= 31 && !am.canScheduleExactAlarms()) {
            emitStatus("need_exact_perm", "SCHEDULE_EXACT_ALARM")
            return
        }
        val pi = pendingFor(id, label)
        try {
            am.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, whenMs, pi)
            exact[id] = ExactAlarm(id, whenMs, label)
            emit(
                JSONObject()
                    .put("type", "scheduled")
                    .put("id", id)
                    .put("whenMs", whenMs)
                    .put("backend", HostPrefs.BACKEND_EXACT),
            )
        } catch (t: SecurityException) {
            emitStatus("need_exact_perm", t.message ?: "denied")
        } catch (t: Throwable) {
            LinkLog.w("alarms.exact failed: ${t.message}")
            emitStatus("error", t.message ?: "exact")
        }
    }

    private fun pendingFor(id: String, label: String): PendingIntent {
        val intent = Intent(context, ExactAlarmReceiver::class.java).apply {
            action = ExactAlarmReceiver.ACTION
            putExtra(ExactAlarmReceiver.EXTRA_ID, id)
            putExtra(ExactAlarmReceiver.EXTRA_LABEL, label)
        }
        return PendingIntent.getBroadcast(
            context,
            id.hashCode(),
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
    }

    private fun emit(o: JSONObject) = onEvent(o.toString())

    private fun emitStatus(state: String, detail: String) {
        emit(
            JSONObject()
                .put("type", "status")
                .put("state", state)
                .put("detail", detail.take(80)),
        )
    }
}
