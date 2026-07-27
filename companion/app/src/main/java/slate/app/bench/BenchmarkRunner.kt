package slate.app.bench

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeout
import slate.app.link.LinkLog
import slate.app.link.LinkMetrics
import slate.app.link.SharedLink
import slate.app.link.SlateGattClient
import slate.diag.BenchStats
import slate.diag.SdpDiag
import slate.dsl.displayList
import slate.frame.SdpFrame
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.random.Random

/**
 * Phone-side measurement gates A/B/D over DIAG channel 7.
 * Requires debug firmware with [sdp::diag::Bench] armed.
 */
class BenchmarkRunner(
    private val gatt: SlateGattClient,
) {
    data class LinkSnapshot(
        val attMtu: Int,
        val phyTx: String,
        val phyRx: String,
        val intervalMs: Double?,
    )

    data class ThruReport(
        val watchKbps: Double,
        val phoneKbps: Double,
        val bytes: Long,
        val elapsedUsWatch: Long,
        val elapsedMsPhone: Double,
        val mbuf: SdpDiag.MbufResult?,
        val link: LinkSnapshot,
        val pass: Boolean,
    )

    data class RttReport(
        val summaryMs: BenchStats.Summary,
        val link: LinkSnapshot,
        val pass: Boolean,
    )

    data class RenderReport(
        val summaryMs: BenchStats.Summary,
        val listBytes: Int,
        val link: LinkSnapshot,
        val pass: Boolean,
    )

    data class FullReport(
        val thru: ThruReport?,
        val rtt: RttReport?,
        val render: RenderReport?,
        val log: String,
    )

    private val waiters = CopyOnWriteArrayList<Pair<Int, CompletableDeferred<ByteArray>>>()

    init {
        gatt.addDiagListener { msg ->
            val op = msg.firstOrNull()?.toInt()?.and(0xFF) ?: return@addDiagListener
            for ((want, def) in waiters.toList()) {
                if (want == op && !def.isCompleted) {
                    waiters.removeAll { it.second === def }
                    def.complete(msg)
                    break
                }
            }
        }
    }

    private fun snapshot(m: LinkMetrics = gatt.metrics.value) = LinkSnapshot(
        attMtu = m.attMtu,
        phyTx = m.phyTx,
        phyRx = m.phyRx,
        intervalMs = m.intervalMs,
    )

    private suspend fun awaitOp(op: Int, timeoutMs: Long): ByteArray {
        val def = CompletableDeferred<ByteArray>()
        waiters += op to def
        try {
            return withTimeout(timeoutMs) { def.await() }
        } catch (e: TimeoutCancellationException) {
            waiters.removeAll { it.second === def }
            throw e
        }
    }

    suspend fun runThroughput(
        totalBytes: Int = 128 * 1024,
        chunkPayload: Int = 4000,
    ): ThruReport {
        require(totalBytes > 0)
        SharedLink.benchmarkPaused = true
        delay(200)
        try {
            val t0 = System.nanoTime()
            if (!gatt.sendMessage(SdpFrame.CHAN_DIAG, SdpDiag.thruStart(totalBytes))) {
                error("THRU_START send failed — is GATT ready?")
            }
            var sent = 0
            val rnd = Random(1)
            while (sent < totalBytes) {
                val n = minOf(chunkPayload, totalBytes - sent)
                val chunk = ByteArray(n) { rnd.nextInt(256).toByte() }
                if (!gatt.sendMessage(SdpFrame.CHAN_DIAG, SdpDiag.thruData(chunk))) {
                    error("THRU_DATA send failed at $sent")
                }
                sent += n
                // Yield so write queue can drain under WNR back-pressure.
                if (sent % (chunkPayload * 4) == 0) delay(1)
            }
            val rsp = awaitOp(SdpDiag.OP_THRU_RESULT, timeoutMs = 60_000)
            val phoneMs = (System.nanoTime() - t0) / 1_000_000.0
            val parsed = SdpDiag.parseThruResult(rsp) ?: error("bad THRU_RESULT")
            val phoneKbps = if (phoneMs > 0) (parsed.bytes / phoneMs) else 0.0
            val mbuf = try {
                if (!gatt.sendMessage(SdpFrame.CHAN_DIAG, SdpDiag.mbufReq())) null
                else SdpDiag.parseMbufResult(awaitOp(SdpDiag.OP_MBUF_RSP, 5_000))
            } catch (_: Exception) {
                null
            }
            return ThruReport(
                watchKbps = parsed.kbps,
                phoneKbps = phoneKbps,
                bytes = parsed.bytes,
                elapsedUsWatch = parsed.elapsedUs,
                elapsedMsPhone = phoneMs,
                mbuf = mbuf,
                link = snapshot(),
                pass = parsed.kbps >= 60.0,
            )
        } finally {
            SharedLink.benchmarkPaused = false
        }
    }

    suspend fun runRtt(samples: Int = 1000, gapMs: Long = 5): RttReport {
        SharedLink.benchmarkPaused = true
        delay(100)
        try {
            val values = ArrayList<Double>(samples)
            repeat(samples) { i ->
                val token = SdpDiag.u64Le(System.nanoTime())
                val t0 = System.nanoTime()
                if (!gatt.sendMessage(SdpFrame.CHAN_DIAG, SdpDiag.rttReq(token))) {
                    error("RTT send failed at sample $i")
                }
                val rsp = awaitOp(SdpDiag.OP_RTT_RSP, timeoutMs = 5_000)
                val ms = (System.nanoTime() - t0) / 1_000_000.0
                val parsed = SdpDiag.parseRttResult(rsp)
                if (parsed == null || !parsed.token.contentEquals(token)) {
                    LinkLog.w("RTT sample $i token mismatch — still recording phone RTT")
                }
                values += ms
                if (gapMs > 0) delay(gapMs)
            }
            val summary = BenchStats.summarize(values)
            return RttReport(
                summaryMs = summary,
                link = snapshot(),
                pass = summary.p95 < 250.0,
            )
        } finally {
            SharedLink.benchmarkPaused = false
        }
    }

    suspend fun runRender(samples: Int = 100): RenderReport {
        SharedLink.benchmarkPaused = true
        delay(100)
        try {
            val list = typicalEightyByteList()
            val values = ArrayList<Double>(samples)
            repeat(samples) { i ->
                if (!gatt.sendMessage(SdpFrame.CHAN_DIAG, SdpDiag.renderReq(list))) {
                    error("RENDER send failed at $i")
                }
                val rsp = awaitOp(SdpDiag.OP_RENDER_RSP, timeoutMs = 5_000)
                val parsed = SdpDiag.parseRenderResult(rsp) ?: error("bad RENDER_RSP")
                values += BenchStats.usToMs(parsed.totalUs)
            }
            val summary = BenchStats.summarize(values)
            return RenderReport(
                summaryMs = summary,
                listBytes = list.size,
                link = snapshot(),
                pass = summary.p95 < 30.0 && summary.mean < 30.0,
            )
        } finally {
            SharedLink.benchmarkPaused = false
        }
    }

    suspend fun runAll(
        thruBytes: Int = 128 * 1024,
        rttSamples: Int = 1000,
        renderSamples: Int = 100,
    ): FullReport {
        val log = StringBuilder()
        fun line(s: String) {
            log.appendLine(s)
            LinkLog.i(s)
        }
        line("=== Slate gates A/B/D ===")
        line(formatLink(snapshot()))

        val thru = try {
            runThroughput(thruBytes).also { r ->
                line(
                    BenchStats.gatePass(
                        "A throughput",
                        r.pass,
                        "watch=${"%.1f".format(r.watchKbps)} kB/s  " +
                            "phone=${"%.1f".format(r.phoneKbps)} kB/s  " +
                            "bytes=${r.bytes}  watch_us=${r.elapsedUsWatch}",
                    ),
                )
                r.mbuf?.let {
                    line(
                        "  mbuf peak=${it.peakUsed}/${it.blockCount} " +
                            "block=${it.blockSize}B free=${it.freeNow}",
                    )
                }
            }
        } catch (t: Throwable) {
            line("FAIL A throughput — ${t.message}")
            null
        }

        val rtt = try {
            runRtt(rttSamples).also { r ->
                line(
                    BenchStats.gatePass(
                        "B tap RTT",
                        r.pass,
                        r.summaryMs.format("ms"),
                    ),
                )
            }
        } catch (t: Throwable) {
            line("FAIL B tap RTT — ${t.message}")
            null
        }

        val render = try {
            runRender(renderSamples).also { r ->
                line(
                    BenchStats.gatePass(
                        "D parse+render",
                        r.pass,
                        "list=${r.listBytes}B  ${r.summaryMs.format("ms")}",
                    ),
                )
            }
        } catch (t: Throwable) {
            line("FAIL D parse+render — ${t.message}")
            null
        }

        line("=== done ===")
        return FullReport(thru, rtt, render, log.toString())
    }

    companion object {
        fun formatLink(s: LinkSnapshot): String =
            "link MTU=${s.attMtu} PHY=${s.phyTx}/${s.phyRx} " +
                "interval=${s.intervalMs?.let { "%.2f ms".format(it) } ?: "—"}"

        /** ~80-byte "typical" notification-style list for gate D. */
        fun typicalEightyByteList(): ByteArray = displayList {
            palette(0, Colors.BLACK)
            palette(1, Colors.WHITE)
            palette(2, rgb(0x07E0))
            clear(pal(0))
            text(
                font = Font.LARGE,
                x = 120,
                y = 40,
                align = Align.CENTER,
                color = pal(1),
                text = "Mail",
            )
            element(id = 1, x = 16, y = 80, w = 208, h = 48) {
                rectRound(16, 80, 208, 48, r = 6, color = rgb(0x2104), style = Style.FILL)
                text(
                    font = Font.LARGE,
                    x = 120,
                    y = 96,
                    align = Align.CENTER,
                    color = pal(1),
                    text = "Hello",
                )
            }
            progressArc(
                cx = 120, cy = 180, r = 28, pct = 40,
                fg = pal(2), bg = rgb(0x4208), width = 3,
            )
            commit()
        }
    }
}
