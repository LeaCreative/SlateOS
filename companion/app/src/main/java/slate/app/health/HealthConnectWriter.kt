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

    suspend fun readTodayStepsAndHr(): Pair<Long?, Long?> {
        val client = clientOrNull() ?: return null to null
        val zone = ZoneId.systemDefault()
        val start = ZonedDateTime.now(zone).toLocalDate().atStartOfDay(zone).toInstant()
        val end = Instant.now()
        val filter = TimeRangeFilter.between(start, end)
        var steps: Long? = null
        var hr: Long? = null
        try {
            val stepResp = client.readRecords(
                ReadRecordsRequest(StepsRecord::class, timeRangeFilter = filter),
            )
            steps = stepResp.records.sumOf { it.count }
        } catch (t: Throwable) {
            LinkLog.w("HC read steps: ${t.message}")
        }
        try {
            val hrResp = client.readRecords(
                ReadRecordsRequest(HeartRateRecord::class, timeRangeFilter = filter),
            )
            val samples = hrResp.records.flatMap { it.samples }
            if (samples.isNotEmpty()) {
                hr = samples.last().beatsPerMinute
            }
        } catch (t: Throwable) {
            LinkLog.w("HC read HR: ${t.message}")
        }
        return steps to hr
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
