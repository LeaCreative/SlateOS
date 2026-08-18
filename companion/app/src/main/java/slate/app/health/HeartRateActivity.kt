package slate.app.health

import android.os.Bundle
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import slate.app.SlateActivity
import slate.app.theme.SlateTitleBar
import slate.app.theme.setSlateContent
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/**
 * Rolling 24-hour BPM vs time of day, fed by watch CONTROL VITALS.
 */
class HeartRateActivity : SlateActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setSlateContent {
            HeartRateScreen()
        }
    }
}

@Composable
fun HeartRateScreen() {
    val context = LocalContext.current
    val log = remember { BpmLog.get(context) }
    val samples by log.samples.collectAsState()
    var now by remember { mutableLongStateOf(System.currentTimeMillis()) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(30_000)
            now = System.currentTimeMillis()
        }
    }
    val zone = remember { ZoneId.systemDefault() }
    val timeFmt = remember { DateTimeFormatter.ofPattern("HH:mm") }
    val latest = samples.lastOrNull()
    val cutoff = now - BpmLogLogic.WINDOW_MS
    val inWindow = samples.filter { it.timeMs >= cutoff }
    val minBpm = inWindow.minOfOrNull { it.bpm }
    val maxBpm = inWindow.maxOfOrNull { it.bpm }
    val avgBpm = if (inWindow.isEmpty()) null else inWindow.sumOf { it.bpm } / inWindow.size

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        topBar = { SlateTitleBar(title = "Heart rate") },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = if (latest != null) "${latest.bpm} BPM" else "— BPM",
                style = MaterialTheme.typography.displaySmall,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                text = when {
                    latest == null ->
                        "No samples yet. Turn Heart rate On in Watch settings " +
                            "and keep the link up."
                    else ->
                        "Last reading ${formatClock(latest.timeMs, zone, timeFmt)}. " +
                            "Rolling last 24 hours vs time of day."
                },
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (minBpm != null && maxBpm != null && avgBpm != null) {
                Text(
                    text = "Min $minBpm  ·  Avg $avgBpm  ·  Max $maxBpm  ·  ${inWindow.size} samples",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            BpmDayGraph(
                samples = inWindow,
                nowMs = now,
                zone = zone,
                timeFmt = timeFmt,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(260.dp),
            )
        }
    }
}

@Composable
private fun BpmDayGraph(
    samples: List<BpmSample>,
    nowMs: Long,
    zone: ZoneId,
    timeFmt: DateTimeFormatter,
    modifier: Modifier = Modifier,
) {
    val lineColor = MaterialTheme.colorScheme.primary
    val gridColor = MaterialTheme.colorScheme.outlineVariant
    val labelColor = MaterialTheme.colorScheme.onSurfaceVariant
    val startMs = nowMs - BpmLogLogic.WINDOW_MS
    val yMin = 40
    val yMax = 200
    val yTicks = listOf(40, 80, 120, 160, 200)
    val midMs = startMs + BpmLogLogic.WINDOW_MS / 2
    val hourTicks = remember(startMs, nowMs, zone) {
        hourBoundaries(startMs, nowMs, zone)
    }
    val textMeasurer = rememberTextMeasurer()
    val tickStyle = TextStyle(
        color = labelColor,
        fontSize = 11.sp,
        fontWeight = FontWeight.Medium,
    )

    Column(modifier = modifier) {
        Canvas(Modifier.weight(1f).fillMaxWidth()) {
            val padL = 36.dp.toPx()
            val padR = 8.dp.toPx()
            val padT = 10.dp.toPx()
            val padB = 10.dp.toPx()
            val plotW = (size.width - padL - padR).coerceAtLeast(1f)
            val plotH = (size.height - padT - padB).coerceAtLeast(1f)

            fun xOf(t: Long): Float {
                val frac = ((t - startMs).toFloat() / BpmLogLogic.WINDOW_MS.toFloat())
                    .coerceIn(0f, 1f)
                return padL + frac * plotW
            }

            fun yOf(bpm: Int): Float {
                val frac = ((bpm - yMin).toFloat() / (yMax - yMin).toFloat())
                    .coerceIn(0f, 1f)
                return padT + (1f - frac) * plotH
            }

            for (tick in yTicks) {
                val y = yOf(tick)
                drawLine(
                    color = gridColor,
                    start = Offset(padL, y),
                    end = Offset(padL + plotW, y),
                    strokeWidth = 1.dp.toPx(),
                )
                val label = tick.toString()
                val layout = textMeasurer.measure(label, tickStyle)
                val ty = (y - layout.size.height / 2f)
                    .coerceIn(0f, size.height - layout.size.height)
                drawText(
                    textMeasurer = textMeasurer,
                    text = label,
                    topLeft = Offset(padL - 6.dp.toPx() - layout.size.width, ty),
                    style = tickStyle,
                )
            }
            for (t in hourTicks) {
                val x = xOf(t)
                drawLine(
                    color = gridColor.copy(alpha = 0.45f),
                    start = Offset(x, padT),
                    end = Offset(x, padT + plotH),
                    strokeWidth = 1.dp.toPx(),
                )
            }

            if (samples.size >= 2) {
                val path = Path()
                samples.forEachIndexed { i, s ->
                    val x = xOf(s.timeMs)
                    val y = yOf(s.bpm)
                    if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
                }
                drawPath(
                    path = path,
                    color = lineColor,
                    style = Stroke(width = 2.5.dp.toPx(), cap = StrokeCap.Round),
                )
            } else if (samples.size == 1) {
                drawCircle(
                    color = lineColor,
                    radius = 4.dp.toPx(),
                    center = Offset(xOf(samples[0].timeMs), yOf(samples[0].bpm)),
                )
            }
        }
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(start = 36.dp, top = 4.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(
                text = formatClock(startMs, zone, timeFmt),
                style = MaterialTheme.typography.labelSmall,
                color = labelColor,
            )
            Text(
                text = formatClock(midMs, zone, timeFmt),
                style = MaterialTheme.typography.labelSmall,
                color = labelColor,
            )
            Text(
                text = formatClock(nowMs, zone, timeFmt),
                style = MaterialTheme.typography.labelSmall,
                color = labelColor,
            )
        }
        Text(
            text = "BPM. Heart rate Off on the watch sends no samples.",
            style = MaterialTheme.typography.bodySmall,
            color = labelColor,
            modifier = Modifier.padding(top = 8.dp),
        )
    }
}

private fun formatClock(timeMs: Long, zone: ZoneId, fmt: DateTimeFormatter): String =
    Instant.ofEpochMilli(timeMs).atZone(zone).format(fmt)

private fun hourBoundaries(startMs: Long, nowMs: Long, zone: ZoneId): List<Long> {
    val start = Instant.ofEpochMilli(startMs).atZone(zone)
    var t = start.plusHours(1).withMinute(0).withSecond(0).withNano(0)
    val end = Instant.ofEpochMilli(nowMs).atZone(zone)
    val out = ArrayList<Long>()
    while (!t.isAfter(end)) {
        out.add(t.toInstant().toEpochMilli())
        t = t.plusHours(1)
    }
    return out
}
