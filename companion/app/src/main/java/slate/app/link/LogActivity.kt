package slate.app.link

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import slate.app.BuildConfig

/**
 * In-app log viewer — the nRF Connect-style log we kept wanting.
 *
 * Reads [LinkLog]'s ring buffer, refreshes while open, and can share the whole
 * thing as text. Deliberately plain views: this screen has to work when the
 * rest of the app is misbehaving.
 */
class LogActivity : Activity() {

    private lateinit var text: TextView
    private lateinit var scroll: ScrollView
    private val handler = Handler(Looper.getMainLooper())
    private var autoScroll = true
    private var lastCount = -1

    /** Freezes the view only — LinkLog keeps recording underneath. */
    private var paused = false

    /**
     * Lines captured at the moment of pausing.
     *
     * Copy and Share must use this, not [LinkLog.snapshot], or pausing freezes
     * the view while copying still takes the live, still-growing buffer —
     * which defeats the entire point of the button.
     */
    private var frozen: List<String>? = null

    private val refresh = object : Runnable {
        override fun run() {
            // Check here as well as at the call sites: removeCallbacks cannot
            // cancel a run that has already been dispatched, and that run
            // re-posts itself — so one stray callback kept the loop alive
            // indefinitely after pausing.
            if (paused) return
            render()
            handler.postDelayed(this, REFRESH_MS)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(16, 16, 16, 16)
        }

        root.addView(
            TextView(this).apply {
                text = "Slate companion ${BuildConfig.VERSION_NAME} " +
                    "(build ${BuildConfig.VERSION_CODE}, ${BuildConfig.BUILD_TYPE})"
                setTypeface(Typeface.MONOSPACE)
                textSize = 12f
            },
        )

        // Two rows of equal-width buttons. One scrollable row hid the buttons
        // entirely; equal weights mean every button always has a visible slot
        // and nothing depends on measured text width.
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.START
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            )
        }
        val bar2 = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.START
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            )
        }
        fun slot() = LinearLayout.LayoutParams(
            0,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            1f,
        )
        // Pause first: a live-updating view cannot be selected or copied from,
        // which is exactly when you most want to read it.
        bar.addView(
            Button(this).apply {
                layoutParams = slot()
                text = "Pause"
                setOnClickListener {
                    paused = !paused
                    text = if (paused) "Resume" else "Pause"
                    if (paused) {
                        frozen = LinkLog.snapshot()
                        // Stop the timer outright. Leaving it running and
                        // relying on render() to bail still re-posted work
                        // every 500 ms, which reset the ScrollView position —
                        // the view kept jumping to the top with no new lines.
                        handler.removeCallbacks(refresh)
                    } else {
                        frozen = null
                        lastCount = -1
                        handler.post(refresh)
                    }
                }
            },
        )
        bar.addView(
            Button(this).apply {
                layoutParams = slot()
                text = "Copy"
                setOnClickListener { copy() }
            },
        )
        bar.addView(
            Button(this).apply {
                layoutParams = slot()
                text = "Share"
                setOnClickListener { share() }
            },
        )
        bar2.addView(
            Button(this).apply {
                layoutParams = slot()
                text = "Clear"
                setOnClickListener {
                    LinkLog.clear()
                    lastCount = -1
                    render()
                }
            },
        )
        // Short label: "Auto-scroll: off" was ellipsised to "all: off", which
        // read like a pause state. "Tail" borrows the tail -f sense.
        bar2.addView(
            Button(this).apply {
                layoutParams = slot()
                text = "Tail on"
                setOnClickListener {
                    autoScroll = !autoScroll
                    text = if (autoScroll) "Tail on" else "Tail off"
                }
            },
        )
        root.addView(bar)
        root.addView(bar2)

        text = TextView(this).apply {
            setTypeface(Typeface.MONOSPACE)
            textSize = 10f
            setTextIsSelectable(true)
            setTextColor(Color.LTGRAY)
        }
        scroll = ScrollView(this).apply {
            addView(text)
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
            ).apply { weight = 1f }
        }
        root.addView(scroll)

        setContentView(root)

        // targetSdk 35 forces edge-to-edge: without this the content lays out
        // from y=0, so the header and both button rows rendered *underneath*
        // the status bar and the ActionBar. They were present in the view
        // hierarchy the whole time — just covered — which is why every layout
        // change appeared to do nothing.
        root.setOnApplyWindowInsetsListener { v, insets ->
            val bars = insets.getInsets(
                android.view.WindowInsets.Type.systemBars() or
                    android.view.WindowInsets.Type.displayCutout(),
            )
            v.setPadding(16, bars.top + 16, 16, bars.bottom + 16)
            insets
        }
        root.requestApplyInsets()

        render()
    }

    override fun onResume() {
        super.onResume()
        if (!paused) handler.post(refresh)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(refresh)
    }

    private fun render() {
        if (paused) return
        val lines = LinkLog.snapshot()
        if (lines.size == lastCount) return
        lastCount = lines.size
        text.text = if (lines.isEmpty()) "(no log entries yet)" else lines.joinToString("\n")
        if (autoScroll && !paused) {
            scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
        }
    }

    private fun copy() {
        val cm = getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager
        cm.setPrimaryClip(android.content.ClipData.newPlainText("Slate log", logText()))
        android.widget.Toast.makeText(
            this,
            "Log copied (${logText().count { it == '\n' }} lines)",
            android.widget.Toast.LENGTH_SHORT,
        ).show()
    }

    private fun lines(): List<String> = frozen ?: LinkLog.snapshot()

    /**
     * Copy exactly what is on screen.
     *
     * This used to rebuild the text from [frozen] / [LinkLog]. If the snapshot
     * taken at pause time was empty — or the buffer had been cleared since —
     * Copy silently produced a header and nothing else, while the view still
     * showed a full log. What you see is now what you get: the TextView is the
     * single source, with the buffer used only as a fallback.
     */
    private fun logText(): String = buildString {
        append("Slate companion ${BuildConfig.VERSION_NAME} ")
        append("(build ${BuildConfig.VERSION_CODE}, ${BuildConfig.BUILD_TYPE})")
        if (paused) append("  [PAUSED]")
        append("\n")
        val shown = text.text?.toString().orEmpty()
        val body = if (shown.isNotBlank() && shown != "(no log entries yet)") {
            shown
        } else {
            lines().joinToString("\n")
        }
        append(if (body.isBlank()) "(log buffer empty)" else body)
    }

    private fun share() {
        val body = logText()
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, "Slate companion log")
            putExtra(Intent.EXTRA_TEXT, body)
        }
        startActivity(Intent.createChooser(intent, "Share log"))
    }

    companion object {
        private const val REFRESH_MS = 500L
    }
}
