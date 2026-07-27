package slate.app.script

import android.graphics.Typeface
import android.os.Bundle
import android.util.TypedValue
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.ComponentActivity
import slate.script.ScriptConsole

/** In-app developer console: slate.log, timings, quotas, governor violations. */
class DevConsoleActivity : ComponentActivity() {

    private lateinit var body: TextView
    private val listener: () -> Unit = { runOnUiThread { refresh() } }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP, 12f, resources.displayMetrics,
        ).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        root.addView(TextView(this).apply {
            text = "Script console"
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
            typeface = Typeface.DEFAULT_BOLD
        })
        root.addView(Button(this).apply {
            text = "Clear"
            setOnClickListener { ScriptConsole.clear(); refresh() }
        })
        body = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
        }
        root.addView(ScrollView(this).apply { addView(body) }, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.MATCH_PARENT,
        ))
        setContentView(root)
        ScriptConsole.addListener(listener)
        refresh()
    }

    override fun onDestroy() {
        ScriptConsole.removeListener(listener)
        super.onDestroy()
    }

    private fun refresh() {
        body.text = ScriptConsole.snapshot().asReversed().joinToString("\n") { e ->
            val ms = e.ms?.let { " ${it}ms" } ?: ""
            "${e.atMs} [${e.appId}] ${e.kind}$ms ${e.message}"
        }.ifBlank { "(empty)" }
    }
}
