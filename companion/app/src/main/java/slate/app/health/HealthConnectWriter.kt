package slate.app.health

import android.content.Context
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.permission.HealthPermission
import androidx.health.connect.client.records.HeartRateRecord
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.records.metadata.Device
import androidx.health.connect.client.records.metadata.Metadata
import androidx.health.connect.client.request.ReadRecordsRequest
import androidx.health.connect.client.time.TimeRangeFilter
import slate.app.link.LinkLog
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime

/**
 * Health Connect read/write for watch vitals. Google Fit (and others) consume
 * records through HC — there is no Google Fit API here.
 */
class HealthConnectWriter(private val context: Context) {

    sealed class ReadResult {
        data class Ok(val stepsToday: Long?, val hrBpm: Long?) : ReadResult()
        data object Denied : ReadResult()
        data class Unavailable(val detail: String) : ReadResult()
        data class Error(val detail: String) : ReadResult()
    }

    fun availability(): String {
        val status = HealthConnectClient.getSdkStatus(context)
        return when (status) {
            HealthConnectClient.SDK_AVAILABLE -> "available"
            HealthConnectClient.SDK_UNAVAILABLE_PROVIDER_UPDATE_REQUIRED -> "update_required"
            else -> "unavailable"
        }
    }

    fun clientOrNull(): HealthConnectClient? {
        if (availability() != "available") return null
        return try {
            HealthConnectClient.getOrCreate(context)
        } catch (t: Throwable) {
            LinkLog.w("HC client: ${t.message}")
            null
        }
    }

    suspend fun hasReadPermissions(): Boolean {
        val client = clientOrNull() ?: return false
        return try {
            val granted = client.permissionController.getGrantedPermissions()
            READ_PERMISSIONS.all { it in granted }
        } catch (t: Throwable) {
            LinkLog.w("HC getGrantedPermissions: ${t.message}")
            false
        }
    }

    suspend fun writeHeartRate(samples: List<Pair<Long, Long>>) {
        if (samples.isEmpty()) return
        val client = clientOrNull() ?: return
        val start = Instant.ofEpochMilli(samples.minOf { it.first })
        val end = Instant.ofEpochMilli(samples.maxOf { it.first }.coerceAtLeast(start.toEpochMilli() + 1))
        val zone = ZoneId.systemDefault().rules.getOffset(start)
        val series = samples.map { (t, bpm) ->
            HeartRateRecord.Sample(
                time = Instant.ofEpochMilli(t),
                beatsPerMinute = bpm.coerceIn(1L, 300L),
            )
        }
        val record = HeartRateRecord(
            startTime = start,
            startZoneOffset = zone,
            endTime = end,
            endZoneOffset = zone,
            samples = series,
            metadata = Metadata(
                recordingMethod = Metadata.RECORDING_METHOD_AUTOMATICALLY_RECORDED,
                device = Device(type = Device.TYPE_WATCH),
            ),
        )
        try {
            client.insertRecords(listOf(record))
            LinkLog.i("HC write HR samples=${samples.size}")
        } catch (t: Throwable) {
            LinkLog.w("HC write HR: ${t.message}")
        }
    }

    suspend fun writeStepsDelta(delta: Long, startMs: Long, endMs: Long) {
        if (delta <= 0L) return
        val client = clientOrNull() ?: return
        val start = Instant.ofEpochMilli(startMs)
        val end = Instant.ofEpochMilli(endMs.coerceAtLeast(startMs + 1))
        val zone = ZoneId.systemDefault().rules.getOffset(start)
        val record = StepsRecord(
            startTime = start,
            startZoneOffset = zone,
            endTime = end,
            endZoneOffset = zone,
            count = delta,
            metadata = Metadata(
                recordingMethod = Metadata.RECORDING_METHOD_AUTOMATICALLY_RECORDED,
                device = Device(type = Device.TYPE_WATCH),
            ),
        )
        try {
            client.insertRecords(listOf(record))
            LinkLog.i("HC write steps delta=$delta")
        } catch (t: Throwable) {
            LinkLog.w("HC write steps: ${t.message}")
        }
    }

    suspend fun readTodayStepsAndHr(): ReadResult {
        when (val avail = availability()) {
            "unavailable", "update_required" -> return ReadResult.Unavailable(avail)
        }
        val client = clientOrNull() ?: return ReadResult.Unavailable("no client")
        if (!hasReadPermissions()) {
            return ReadResult.Denied
        }
        val zone = ZoneId.systemDefault()
        val start = ZonedDateTime.now(zone).toLocalDate().atStartOfDay(zone).toInstant()
        val end = Instant.now()
        val filter = TimeRangeFilter.between(start, end)
        var steps: Long? = null
        var hr: Long? = null
        var denied = false
        try {
            val stepResp = client.readRecords(
                ReadRecordsRequest(StepsRecord::class, timeRangeFilter = filter),
            )
            steps = stepResp.records.sumOf { it.count }
        } catch (t: SecurityException) {
            LinkLog.w("HC read steps: ${t.message}")
            denied = true
        } catch (t: Throwable) {
            if (t.message?.contains("SecurityException") == true ||
                t.message?.contains("READ_STEPS") == true
            ) {
                LinkLog.w("HC read steps: ${t.message}")
                denied = true
            } else {
                LinkLog.w("HC read steps: ${t.message}")
                return ReadResult.Error((t.message ?: "steps").take(80))
            }
        }
        try {
            val hrResp = client.readRecords(
                ReadRecordsRequest(HeartRateRecord::class, timeRangeFilter = filter),
            )
            val samples = hrResp.records.flatMap { it.samples }
            if (samples.isNotEmpty()) {
                hr = samples.last().beatsPerMinute
            }
        } catch (t: SecurityException) {
            LinkLog.w("HC read HR: ${t.message}")
            denied = true
        } catch (t: Throwable) {
            if (t.message?.contains("SecurityException") == true ||
                t.message?.contains("READ_HEART_RATE") == true
            ) {
                LinkLog.w("HC read HR: ${t.message}")
                denied = true
            } else {
                LinkLog.w("HC read HR: ${t.message}")
                // Keep steps if we got them.
            }
        }
        if (denied && steps == null && hr == null) {
            return ReadResult.Denied
        }
        return ReadResult.Ok(stepsToday = steps, hrBpm = hr)
    }

    companion object {
        val WRITE_PERMISSIONS: Set<String> = setOf(
            HealthPermission.getWritePermission(StepsRecord::class),
            HealthPermission.getWritePermission(HeartRateRecord::class),
        )
        val READ_PERMISSIONS: Set<String> = setOf(
            HealthPermission.getReadPermission(StepsRecord::class),
            HealthPermission.getReadPermission(HeartRateRecord::class),
        )
        val ALL_PERMISSIONS: Set<String> = WRITE_PERMISSIONS + READ_PERMISSIONS
    }
}
