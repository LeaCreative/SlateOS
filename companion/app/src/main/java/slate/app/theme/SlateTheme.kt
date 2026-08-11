package slate.app.theme

import android.graphics.Color as AndroidColor
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

/** Material3 colours driven by [AppThemePrefs], not only system uiMode. */
@Composable
fun SlateTheme(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val dark = AppThemePrefs.mode(context) == AppThemeMode.Dark
    MaterialTheme(
        colorScheme = if (dark) darkColorScheme() else lightColorScheme(),
        content = content,
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SlateTitleBar(
    title: String,
    actions: @Composable RowScope.() -> Unit = {},
) {
    val context = LocalContext.current
    val switchTo = when (AppThemePrefs.mode(context)) {
        AppThemeMode.Dark -> "Light"
        AppThemeMode.Light -> "Dark"
    }
    TopAppBar(
        title = { Text(title) },
        windowInsets = WindowInsets.safeDrawing.only(
            WindowInsetsSides.Horizontal + WindowInsetsSides.Top,
        ),
        actions = {
            OutlinedButton(
                onClick = { AppThemePrefs.toggle(context) },
                shape = RoundedCornerShape(12.dp),
            ) {
                Text(switchTo)
            }
            actions()
        },
    )
}

fun AppCompatActivity.setSlateContent(content: @Composable () -> Unit) {
    val dark = AppThemePrefs.mode(this) == AppThemeMode.Dark
    enableEdgeToEdge(
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
    setContent {
        SlateTheme {
            Surface(
                modifier = Modifier.fillMaxSize(),
                color = MaterialTheme.colorScheme.background,
            ) {
                content()
            }
        }
    }
}
