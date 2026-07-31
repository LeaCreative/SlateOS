package slate.app.link

import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.core.view.setPadding

/**
 * Operator-facing one-slot BLE contention help (I-16).
 * Cross-links docs/flash-sealed.md step 7.
 */
class TroubleshootingActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = dp(16)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad)
        }
        root.addView(text("Troubleshooting — BLE link slot", 20f, true))
        root.addView(text(LinkContention.ONE_SLOT_SUMMARY, 14f, false))
        root.addView(text(" ", 8f, false))
        root.addView(text("If connect / HELLO / confirm / DFU / OTA stalls", 16f, true))
        root.addView(
            text(
                "Slate detects when another app on this phone already holds the " +
                    "watch (GATT connected list), or when the watch is bonded but " +
                    "silent (likely a foreign central). Fix that first — do not wait " +
                    "for a timeout.",
                14f,
                false,
            ),
        )
        root.addView(text(" ", 8f, false))
        root.addView(text("Remediation", 16f, true))
        root.addView(text(LinkContention.remediationBody(this), 14f, false))
        SharedLink.lastContentionMessage?.let { msg ->
            root.addView(text(" ", 8f, false))
            root.addView(text("Last detected contention", 16f, true))
            root.addView(text(msg, 14f, false))
        }
        root.addView(text(" ", 8f, false))
        root.addView(
            text(
                "Sealed install confirm also needs the free slot for 10 s " +
                    "(docs/flash-sealed.md steps 7–8). Soft-brick recovery: " +
                    "docs/recovery-sealed.md.",
                13f,
                false,
            ),
        )
        root.addView(button("Back") { finish() })
        setContentView(ScrollView(this).apply { addView(root) })
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
            if (bold) setTypeface(typeface, android.graphics.Typeface.BOLD)
            setPadding(0, dp(6), 0, dp(6))
            gravity = Gravity.START
        }

    private fun button(label: String, onClick: () -> Unit): Button =
        Button(this).apply {
            text = label
            gravity = Gravity.START or Gravity.CENTER_VERTICAL
            setOnClickListener { onClick() }
        }
}
