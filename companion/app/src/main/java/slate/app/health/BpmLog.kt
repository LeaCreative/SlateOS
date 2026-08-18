package slate.app.health

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONArray
import slate.app.link.LinkLog
import java.io.File

data class BpmSample(
    val timeMs: Long,
    val bpm: Int,
)

/**
 * Rolling 24-hour BPM series. Pure so host tests can exercise the window
 * without Android. The watch often repeats the same BPM every few seconds;
 * unchanged readings are kept at most once per [MIN_INTERVAL_MS].
 */
object BpmLogLogic {
    const val WINDOW_MS = 24L * 60L * 60L * 1000L
    const val MAX_SAMPLES = 2_880
    const val MIN_INTERVAL_MS = 30_000L
    const val BPM_MIN = 30
    const val BPM_MAX = 220

    fun append(
        existing: List<BpmSample>,
        timeMs: Long,
        bpm: Int,
        nowMs: Long = timeMs,
    ): List<BpmSample> {
        if (bpm !in BPM_MIN..BPM_MAX || timeMs <= 0L) return existing
        val cutoff = nowMs - WINDOW_MS
        if (timeMs < cutoff) {
            val kept = existing.filter { it.timeMs >= cutoff }
            return if (kept.size == existing.size) existing else kept
        }
        val kept = ArrayList<BpmSample>(existing.size + 1)
        for (s in existing) {
            if (s.timeMs >= cutoff) kept.add(s)
        }
        val prev = kept.lastOrNull()
        if (prev != null) {
            if (timeMs < prev.timeMs) {
                return if (kept.size == existing.size) existing else kept
            }
            if (prev.bpm == bpm && timeMs - prev.timeMs < MIN_INTERVAL_MS) {
                return if (kept.size == existing.size) existing else kept
            }
        }
        kept.add(BpmSample(timeMs, bpm))
        while (kept.size > MAX_SAMPLES) {
            kept.removeAt(0)
        }
        return kept
    }

    fun serialize(samples: List<BpmSample>): String {
        val arr = JSONArray()
        for (s in samples) {
            arr.put(JSONArray().put(s.timeMs).put(s.bpm))
        }
        return arr.toString()
    }

    fun parse(json: String): List<BpmSample> {
        if (json.isBlank()) return emptyList()
        val arr = JSONArray(json)
        val out = ArrayList<BpmSample>(arr.length())
        for (i in 0 until arr.length()) {
            val row = arr.optJSONArray(i) ?: continue
            if (row.length() < 2) continue
            val t = row.optLong(0, 0L)
            val b = row.optInt(1, 0)
            if (t > 0L && b in BPM_MIN..BPM_MAX) {
                out.add(BpmSample(t, b))
            }
        }
        return out
    }
}

/**
 * Process-wide BPM log. Always records valid watch VITALS, even when Health
 * Connect sync is off — the graph is a companion feature, not an HC write.
 */
class BpmLog private constructor(private val filesDir: File) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val mutex = Mutex()
    private val file = File(filesDir, FILE_NAME)
    private val _samples = MutableStateFlow(loadUnlocked())
    val samples: StateFlow<List<BpmSample>> = _samples.asStateFlow()

    fun record(timeMs: Long, bpm: Int) {
        scope.launch {
            mutex.withLock {
                val next = BpmLogLogic.append(_samples.value, timeMs, bpm)
                if (next === _samples.value) return@withLock
                _samples.value = next
                saveUnlocked(next)
            }
        }
    }

    private fun loadUnlocked(): List<BpmSample> {
        if (!file.exists()) return emptyList()
        return try {
            val loaded = BpmLogLogic.parse(file.readText())
            val now = System.currentTimeMillis()
            var rebuilt = emptyList<BpmSample>()
            for (s in loaded) {
                rebuilt = BpmLogLogic.append(rebuilt, s.timeMs, s.bpm, now)
            }
            rebuilt
        } catch (t: Throwable) {
            LinkLog.w("bpm log load: ${t.message}")
            emptyList()
        }
    }

    private fun saveUnlocked(samples: List<BpmSample>) {
        try {
            val tmp = File(filesDir, "$FILE_NAME.tmp")
            tmp.writeText(BpmLogLogic.serialize(samples))
            if (!tmp.renameTo(file)) {
                tmp.copyTo(file, overwrite = true)
                tmp.delete()
            }
        } catch (t: Throwable) {
            LinkLog.w("bpm log save: ${t.message}")
        }
    }

    companion object {
        private const val FILE_NAME = "bpm_log.json"

        @Volatile
        private var instance: BpmLog? = null

        fun get(context: Context): BpmLog {
            instance?.let { return it }
            return synchronized(this) {
                instance ?: BpmLog(context.applicationContext.filesDir).also { instance = it }
            }
        }
    }
}
