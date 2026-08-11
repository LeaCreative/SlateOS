package slate.app

import android.content.Intent
import android.os.Bundle
import android.widget.LinearLayout
import android.widget.TextView
import slate.app.bench.BenchmarkActivity
import slate.app.link.LinkForegroundService
import slate.app.script.DevConsoleActivity
import slate.app.ui.SimpleViews

/**
 * Operator / bring-up tools. Not needed for normal pairing and daily use.
 *
 * RTT is measured under **Benchmarks** (gate B), not as a one-shot here.
 *
 * **Script console** — live log of JS sub-app runtime events (`slate.log`,
 * timings, quota / governor violations). Not an interactive REPL.
 */
class DebugActivity : SlateActivity() {

    private lateinit var statusView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
        }

        root.addView(
            SimpleViews.text(
                this,
                "Bring-up and diagnostics. DIAG tools need debug firmware " +
                    "(SLATE_BLE_DIAG=1).",
                14f,
                false,
            ),
        )
        statusView = SimpleViews.text(this, "", 13f, false).also { root.addView(it) }

        root.addView(
            SimpleViews.button(this, "Open TestApp") {
                LinkForegroundService.openTestApp(this)
                statusView.text = "Requested TestApp focus (Kotlin transport probe)"
            },
        )
        root.addView(
            SimpleViews.button(this, "Open Notifications") {
                LinkForegroundService.openNotifications(this)
                statusView.text =
                    "Requested Kotlin Notifications focus (watch shade is primary UX)"
            },
        )
        root.addView(
            SimpleViews.button(this, "Script console") {
                startActivity(Intent(this, DevConsoleActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Troubleshooting (BLE one-slot)") {
                startActivity(Intent(this, slate.app.link.TroubleshootingActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "View log") {
                startActivity(Intent(this, slate.app.link.LogActivity::class.java))
            },
        )
        root.addView(
            SimpleViews.button(this, "Benchmarks (gates A / B / D)") {
                startActivity(Intent(this, BenchmarkActivity::class.java))
            },
        )

        SimpleViews.setContentWithAppBar(
            this,
            title = "Debug",
            body = root,
        )
    }
}
