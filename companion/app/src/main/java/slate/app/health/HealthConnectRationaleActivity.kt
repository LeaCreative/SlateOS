package slate.app.health

import android.os.Bundle
import android.widget.LinearLayout
import android.widget.ScrollView
import slate.app.SlateActivity
import slate.app.ui.SimpleViews

/**
 * Privacy / permission rationale for Health Connect. Required so Android 14+
 * will show the HC permission UI — without the VIEW_PERMISSION_USAGE /
 * ACTION_SHOW_PERMISSIONS_RATIONALE entry points, Grant returns empty silently.
 */
class HealthConnectRationaleActivity : SlateActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(SimpleViews.text(this, "Health Connect", 20f, true))
        root.addView(
            SimpleViews.text(
                this,
                "Slate reads today's steps and heart rate from Health Connect " +
                    "for the Health watch sub-app.",
                14f,
                false,
            ),
        )
        root.addView(
            SimpleViews.text(
                this,
                "Slate writes steps and BPM from the PineTime into Health Connect " +
                    "so other apps (for example Google Fit) can use them.",
                14f,
                false,
            ),
        )
        root.addView(
            SimpleViews.text(
                this,
                "Slate does not upload this data to a Slate cloud service.",
                14f,
                false,
            ),
        )
        setContentView(
            ScrollView(this).apply { addView(root) },
        )
    }
}
