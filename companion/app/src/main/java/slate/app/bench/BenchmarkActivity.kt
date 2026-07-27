package slate.app.bench

import android.graphics.Typeface
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import slate.app.link.SharedLink

class BenchmarkActivity : ComponentActivity() {

    private lateinit var out: TextView
    private lateinit var status: TextView
    private var running = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }

        root.addView(text("Slate benchmarks (gates A/B/D)", 20f, true))
        root.addView(
            text(
                "Needs debug firmware with DIAG bench. Pauses clock push while running.\n" +
                    "A: ≥60 kB/s   B: RTT p95 <250 ms   D: parse+render <30 ms",
                13f,
                false,
            ),
        )

        status = text("Idle", 14f, false).also { root.addView(it) }
        out = text("", 12f, false).also {
            it.typeface = Typeface.MONOSPACE
            root.addView(it)
        }

        root.addView(button("Run all (128 KB + 1000 RTT + 100 render)") {
            runBench { it.runAll() }
        })
        root.addView(button("A — Sustained throughput (128 KB)") {
            runBench {
                val r = it.runThroughput()
                buildString {
                    appendLine(BenchmarkRunner.formatLink(r.link))
                    appendLine(
                        "watch=${"%.1f".format(r.watchKbps)} kB/s  " +
                            "phone=${"%.1f".format(r.phoneKbps)} kB/s",
                    )
                    appendLine("bytes=${r.bytes}  watch_us=${r.elapsedUsWatch}")
                    r.mbuf?.let { m ->
                        appendLine(
                            "mbuf peak=${m.peakUsed}/${m.blockCount} " +
                                "block=${m.blockSize} free=${m.freeNow}",
                        )
                    }
                    appendLine(if (r.pass) "PASS ≥60 kB/s" else "FAIL <60 kB/s")
                }
            }
        })
        root.addView(button("B — RTT 1000 samples") {
            runBench {
                val r = it.runRtt()
                buildString {
                    appendLine(BenchmarkRunner.formatLink(r.link))
                    appendLine(r.summaryMs.format("ms"))
                    appendLine(if (r.pass) "PASS p95 <250 ms" else "FAIL p95 ≥250 ms")
                }
            }
        })
        root.addView(button("D — Render timing (100× ~80 B list)") {
            runBench {
                val r = it.runRender()
                buildString {
                    appendLine(BenchmarkRunner.formatLink(r.link))
                    appendLine("list=${r.listBytes} B")
                    appendLine(r.summaryMs.format("ms"))
                    appendLine(if (r.pass) "PASS <30 ms" else "FAIL ≥30 ms")
                }
            }
        })

        setContentView(ScrollView(this).apply { addView(root) })
    }

    private fun runBench(block: suspend (BenchmarkRunner) -> Any) {
        if (running) {
            status.text = "Already running…"
            return
        }
        if (!SharedLink.gatt(this).metrics.value.connected) {
            status.text = "Not connected — associate + start link first"
            return
        }
        running = true
        status.text = "Running…"
        out.text = ""
        lifecycleScope.launch {
            try {
                val runner = BenchmarkRunner(SharedLink.gatt(this@BenchmarkActivity))
                val result = withContext(Dispatchers.Default) { block(runner) }
                val text = when (result) {
                    is BenchmarkRunner.FullReport -> result.log
                    is CharSequence -> result.toString()
                    else -> result.toString()
                }
                out.text = text
                status.text = "Done"
            } catch (t: Throwable) {
                status.text = "Error"
                out.text = t.stackTraceToString()
            } finally {
                running = false
            }
        }
    }

    private fun dp(v: Int): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP,
            v.toFloat(),
            resources.displayMetrics,
        ).toInt()

    private fun text(value: String, sp: Float, bold: Boolean): TextView =
        TextView(this).apply {
            text = value
            setTextSize(TypedValue.COMPLEX_UNIT_SP, sp)
            if (bold) typeface = Typeface.DEFAULT_BOLD
            setPadding(0, dp(6), 0, dp(6))
        }

    private fun button(label: String, onClick: () -> Unit): Button =
        Button(this).apply {
            text = label
            gravity = Gravity.START or Gravity.CENTER_VERTICAL
            setOnClickListener { onClick() }
        }
}
