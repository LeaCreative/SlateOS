package slate.app.ui

import android.graphics.Color as AndroidColor
import android.graphics.Typeface
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import com.google.android.material.button.MaterialButton
import slate.app.theme.AppThemeMode
import slate.app.theme.AppThemePrefs

/** Shared imperative UI bits for the companion’s View-based screens. */
object SimpleViews {
    fun dp(activity: AppCompatActivity, v: Int): Int =
        TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP,
            v.toFloat(),
            activity.resources.displayMetrics,
        ).toInt()

    fun text(
        activity: AppCompatActivity,
        value: String,
        sp: Float,
        bold: Boolean,
    ): TextView =
        TextView(activity).apply {
            text = value
            setTextSize(TypedValue.COMPLEX_UNIT_SP, sp)
            if (bold) typeface = Typeface.DEFAULT_BOLD
            setPadding(0, dp(activity, 6), 0, dp(activity, 6))
        }

    /** Outlined Material3 button (same chrome as the Light/Dark control). */
    fun button(
        activity: AppCompatActivity,
        label: String,
        onClick: () -> Unit,
    ): MaterialButton =
        MaterialButton(
            activity,
            null,
            com.google.android.material.R.attr.materialButtonOutlinedStyle,
        ).apply {
            text = label
            isAllCaps = false
            cornerRadius = dp(activity, 12)
            gravity = Gravity.START or Gravity.CENTER_VERTICAL
            setOnClickListener { onClick() }
        }

    fun titleBar(activity: AppCompatActivity, title: String): LinearLayout {
        val switchTo = when (AppThemePrefs.mode(activity)) {
            AppThemeMode.Dark -> "Light"
            AppThemeMode.Light -> "Dark"
        }
        return LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(
                text(activity, title, 22f, true).apply {
                    layoutParams = LinearLayout.LayoutParams(
                        0,
                        LinearLayout.LayoutParams.WRAP_CONTENT,
                        1f,
                    )
                },
            )
            addView(
                MaterialButton(
                    activity,
                    null,
                    com.google.android.material.R.attr.materialButtonOutlinedStyle,
                ).apply {
                    text = switchTo
                    isAllCaps = false
                    cornerRadius = dp(activity, 12)
                    setOnClickListener { AppThemePrefs.toggle(activity) }
                },
            )
        }
    }

    /**
     * Fixed title bar (status-bar insets applied here) + scrollable body below.
     * Title does not live in the scroll content and cannot slide under the bars.
     */
    fun setContentWithAppBar(
        activity: AppCompatActivity,
        title: String,
        body: View,
    ) {
        val dark = AppThemePrefs.mode(activity) == AppThemeMode.Dark
        activity.enableEdgeToEdge(
            statusBarStyle = if (dark) {
                SystemBarStyle.dark(AndroidColor.TRANSPARENT)
            } else {
                SystemBarStyle.light(AndroidColor.TRANSPARENT, AndroidColor.TRANSPARENT)
            },
            navigationBarStyle = if (dark) {
                SystemBarStyle.dark(AndroidColor.TRANSPARENT)
            } else {
                SystemBarStyle.light(AndroidColor.TRANSPARENT, AndroidColor.TRANSPARENT)
            },
        )

        val hPad = dp(activity, 16)
        val bar = titleBar(activity, title)
        val scroll = when (body) {
            is ScrollView -> body
            else -> ScrollView(activity).apply { addView(body) }
        }
        scroll.clipToPadding = true

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            addView(
                bar,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
            addView(
                scroll,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    0,
                    1f,
                ),
            )
        }

        ViewCompat.setOnApplyWindowInsetsListener(root) { _, windowInsets ->
            val bars = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            bar.setPadding(hPad, bars.top + hPad / 2, hPad, hPad / 2)
            scroll.setPadding(hPad, hPad / 2, hPad, bars.bottom + hPad)
            windowInsets
        }

        activity.setContentView(root)
        ViewCompat.requestApplyInsets(root)
    }
}
