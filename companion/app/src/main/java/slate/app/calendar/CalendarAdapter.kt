package slate.app.calendar

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.provider.CalendarContract
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import slate.app.link.LinkLog

/** Next N calendar instances from [CalendarContract]; JS sees capped JSON only. */
class CalendarAdapter(
    private val context: Context,
    private val scope: CoroutineScope,
    private val onEvent: (json: String) -> Unit,
) {
    private var job: Job? = null

    fun fetch(limit: Int) {
        job?.cancel()
        emit(JSONObject().put("type", "status").put("state", "loading"))
        job = scope.launch {
            if (!hasPermission()) {
                emit(
                    JSONObject()
                        .put("type", "status")
                        .put("state", "denied")
                        .put("detail", "READ_CALENDAR"),
                )
                return@launch
            }
            try {
                val capped = limit.coerceIn(1, MAX_LIMIT)
                val items = withContext(Dispatchers.IO) { queryUpcoming(capped) }
                val arr = JSONArray()
                for (it in items) {
                    arr.put(
                        JSONObject()
                            .put("id", it.id)
                            .put("title", it.title)
                            .put("startMs", it.startMs)
                            .put("endMs", it.endMs)
                            .put("allDay", it.allDay)
                            .put("location", it.location),
                    )
                }
                emit(JSONObject().put("type", "events").put("items", arr))
            } catch (t: Throwable) {
                LinkLog.w("calendar.fetch failed: ${t.message}")
                emit(
                    JSONObject()
                        .put("type", "status")
                        .put("state", "error")
                        .put("detail", (t.message ?: "query").take(80)),
                )
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private fun emit(o: JSONObject) = onEvent(o.toString())

    private fun hasPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.READ_CALENDAR) ==
            PackageManager.PERMISSION_GRANTED

    private data class Row(
        val id: String,
        val title: String,
        val startMs: Long,
        val endMs: Long,
        val allDay: Boolean,
        val location: String,
    )

    private fun queryUpcoming(limit: Int): List<Row> {
        val now = System.currentTimeMillis()
        val end = now + LOOKAHEAD_MS
        val uri = CalendarContract.Instances.CONTENT_URI.buildUpon()
            .appendPath(now.toString())
            .appendPath(end.toString())
            .build()
        val projection = arrayOf(
            CalendarContract.Instances.EVENT_ID,
            CalendarContract.Instances.TITLE,
            CalendarContract.Instances.BEGIN,
            CalendarContract.Instances.END,
            CalendarContract.Instances.ALL_DAY,
            CalendarContract.Instances.EVENT_LOCATION,
        )
        val out = ArrayList<Row>(limit)
        context.contentResolver.query(
            uri,
            projection,
            null,
            null,
            "${CalendarContract.Instances.BEGIN} ASC",
        )?.use { c ->
            val iId = c.getColumnIndex(CalendarContract.Instances.EVENT_ID)
            val iTitle = c.getColumnIndex(CalendarContract.Instances.TITLE)
            val iBegin = c.getColumnIndex(CalendarContract.Instances.BEGIN)
            val iEnd = c.getColumnIndex(CalendarContract.Instances.END)
            val iAll = c.getColumnIndex(CalendarContract.Instances.ALL_DAY)
            val iLoc = c.getColumnIndex(CalendarContract.Instances.EVENT_LOCATION)
            while (c.moveToNext() && out.size < limit) {
                val title = (if (iTitle >= 0) c.getString(iTitle) else null)
                    ?.trim().orEmpty().ifEmpty { "(no title)" }
                val loc = (if (iLoc >= 0) c.getString(iLoc) else null)?.trim().orEmpty()
                out += Row(
                    id = if (iId >= 0) c.getLong(iId).toString() else out.size.toString(),
                    title = title.take(48),
                    startMs = if (iBegin >= 0) c.getLong(iBegin) else now,
                    endMs = if (iEnd >= 0) c.getLong(iEnd) else now,
                    allDay = iAll >= 0 && c.getInt(iAll) == 1,
                    location = loc.take(32),
                )
            }
        }
        return out
    }

    companion object {
        private const val MAX_LIMIT = 12
        private const val LOOKAHEAD_MS = 14L * 24 * 60 * 60 * 1000
    }
}
