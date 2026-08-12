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
        // Do not emit "loading" here — it races ahead of watch() snapshots and
        // can leave the JS UI stuck on Reading... after a denied HC read.
        job = scope.launch {
            when (val result = writer.readTodayStepsAndHr()) {
                is HealthConnectWriter.ReadResult.Unavailable -> {
                    emit(
                        JSONObject()
                            .put("type", "status")
                            .put("state", "hc_unavailable")
                            .put("detail", result.detail),
                    )
                }
                is HealthConnectWriter.ReadResult.Denied -> {
                    LinkLog.i("health.fetch denied — grant HC in Phone bridges")
                    emit(
                        JSONObject()
                            .put("type", "status")
                            .put("state", "denied")
                            .put("detail", "Grant Health Connect in Phone bridges"),
                    )
                }
                is HealthConnectWriter.ReadResult.Error -> {
                    emit(
                        JSONObject()
                            .put("type", "status")
                            .put("state", "error")
                            .put("detail", result.detail),
                    )
                }
                is HealthConnectWriter.ReadResult.Ok -> {
                    val o = JSONObject()
                        .put("type", "snapshot")
                        .put("source", "hc")
                    if (result.stepsToday != null) {
                        o.put("stepsToday", result.stepsToday)
                    } else {
                        o.put("stepsToday", JSONObject.NULL)
                    }
                    if (result.hrBpm != null) {
                        o.put("hrBpm", result.hrBpm)
                    } else {
                        o.put("hrBpm", JSONObject.NULL)
                    }
                    emit(o)
                }
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
