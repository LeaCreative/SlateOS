package slate.app.health

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.app.settings.HostPrefs
import slate.session.Vitals
import java.util.concurrent.CopyOnWriteArrayList

/**
 * Buffers watch vitals (CONTROL VITALS) and flushes to Health Connect on the
 * planned cadence. Also serves JS [HealthAdapter] snapshots.
 */
class VitalsIngest(
    private val context: Context,
    private val scope: CoroutineScope,
) {
    private val prefs = HostPrefs(context)
    private val writer = HealthConnectWriter(context)
    private val mutex = Mutex()
    private val hrSamples = CopyOnWriteArrayList<Pair<Long, Long>>() // timeMs, bpm
    private var lastSteps: Long = -1L
    private var lastStepsWritten: Long = -1L
    private var lastStepsWriteMs: Long = 0L
    private var lastHrFlushMs: Long = 0L
    private var lastSeenMs: Long = 0L
    private var lastBpm: Int = 0
    private var flushJob: Job? = null

    fun onVitals(snap: Vitals.Snapshot) {
        if (!prefs.healthConnectSync) return
        val now = System.currentTimeMillis()
        lastSeenMs = now
        lastSteps = snap.steps
        if (snap.bpm in 30..220) {
            lastBpm = snap.bpm
            hrSamples += now to snap.bpm.toLong()
            while (hrSamples.size > MAX_HR_SAMPLES) {
                hrSamples.removeAt(0)
            }
        }
        maybeScheduleFlush(now)
    }

    fun latestWatchJson(): JSONObject =
        JSONObject()
            .put("type", "snapshot")
            .put("stepsToday", if (lastSteps >= 0) lastSteps else JSONObject.NULL)
            .put("hrBpm", if (lastBpm > 0) lastBpm else JSONObject.NULL)
            .put("source", "watch")
            .put("ageMs", if (lastSeenMs > 0) System.currentTimeMillis() - lastSeenMs else JSONObject.NULL)

    fun flushNow() {
        scope.launch { flush(force = true) }
    }

    private fun maybeScheduleFlush(now: Long) {
        val dueHr = hrSamples.isNotEmpty() && now - lastHrFlushMs >= HR_FLUSH_MS
        val dueSteps = lastSteps >= 0 && (
            (lastSteps != lastStepsWritten && now - lastStepsWriteMs >= STEPS_MIN_MS) ||
                (now - lastStepsWriteMs >= STEPS_KEEPALIVE_MS)
            )
        if (!dueHr && !dueSteps) return
        if (flushJob?.isActive == true) return
        flushJob = scope.launch { flush(force = false) }
    }

    private suspend fun flush(force: Boolean) = mutex.withLock {
        if (!prefs.healthConnectSync && !force) return
        val now = System.currentTimeMillis()
        try {
            withContext(Dispatchers.IO) {
                if (hrSamples.isNotEmpty() && (force || now - lastHrFlushMs >= HR_FLUSH_MS)) {
                    val batch = hrSamples.toList()
                    hrSamples.clear()
                    writer.writeHeartRate(batch)
                    lastHrFlushMs = now
                }
                if (lastSteps >= 0 &&
                    (force || lastSteps != lastStepsWritten || now - lastStepsWriteMs >= STEPS_KEEPALIVE_MS)
                ) {
                    val start = if (lastStepsWriteMs > 0) lastStepsWriteMs else now - STEPS_MIN_MS
                    val delta = if (lastStepsWritten >= 0) {
                        (lastSteps - lastStepsWritten).coerceAtLeast(0)
                    } else {
                        lastSteps.coerceAtLeast(0)
                    }
                    if (delta > 0 || force) {
                        writer.writeStepsDelta(delta.coerceAtLeast(0), start, now)
                    }
                    lastStepsWritten = lastSteps
                    lastStepsWriteMs = now
                }
            }
        } catch (t: Throwable) {
            LinkLog.w("vitals.flush: ${t.message}")
        }
    }

    companion object {
        private const val MAX_HR_SAMPLES = 120
        private const val HR_FLUSH_MS = 90_000L
        private const val STEPS_MIN_MS = 60_000L
        private const val STEPS_KEEPALIVE_MS = 300_000L
    }
}
