package slate.app.health

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import org.json.JSONObject
import slate.app.link.LinkLog

/** JS-facing health reads (HC aggregates + latest watch buffer). */
class HealthAdapter(
    private val context: Context,
    private val scope: CoroutineScope,
    private val vitals: VitalsIngest,
    private val onEvent: (json: String) -> Unit,
) {
    private val writer = HealthConnectWriter(context)
    private var job: Job? = null

    fun fetch() {
        job?.cancel()
        emit(JSONObject().put("type", "status").put("state", "loading"))
        job = scope.launch {
            when (writer.availability()) {
                "unavailable", "update_required" -> {
                    emit(
                        JSONObject()
                            .put("type", "status")
                            .put("state", "hc_unavailable")
                            .put("detail", writer.availability()),
                    )
                    return@launch
                }
            }
            try {
                val (steps, hr) = writer.readTodayStepsAndHr()
                val o = JSONObject()
                    .put("type", "snapshot")
                    .put("source", "hc")
                if (steps != null) o.put("stepsToday", steps) else o.put("stepsToday", JSONObject.NULL)
                if (hr != null) o.put("hrBpm", hr) else o.put("hrBpm", JSONObject.NULL)
                emit(o)
            } catch (t: SecurityException) {
                emit(
                    JSONObject()
                        .put("type", "status")
                        .put("state", "denied")
                        .put("detail", "HC permission"),
                )
            } catch (t: Throwable) {
                LinkLog.w("health.fetch: ${t.message}")
                emit(
                    JSONObject()
                        .put("type", "status")
                        .put("state", "error")
                        .put("detail", (t.message ?: "read").take(80)),
                )
            }
        }
    }

    fun watch() {
        emit(vitals.latestWatchJson())
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private fun emit(o: JSONObject) = onEvent(o.toString())
}
