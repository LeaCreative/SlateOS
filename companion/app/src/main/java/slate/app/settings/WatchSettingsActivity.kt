package slate.app.settings

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import slate.session.WatchSettings

/**
 * The phone's view of the watch's own settings.
 *
 * Deliberately not a Save button. The same three settings are editable on the
 * watch (swipe left-to-right from the face), the two copies sync in both
 * directions, and a screen holding unsaved edits would have to decide what to do
 * when the watch changed underneath it. Each control writes through
 * immediately; [WatchSettingsStore] stamps the revision and the link sends it.
 */
class WatchSettingsActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val store = WatchSettingsStore.get(this)
        setContent {
            Surface(modifier = Modifier.fillMaxSize()) {
                WatchSettingsScreen(store)
            }
        }
    }
}

@Composable
fun WatchSettingsScreen(store: WatchSettingsStore) {
    // Collected, not remembered: the watch can change these while the screen is
    // open and the change must appear here without the user doing anything.
    val settings by store.settings.collectAsState()
    val pending by store.pendingSend.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            // targetSdk 35 draws edge-to-edge, so without this the first row
            // sits under the system bar and reads as clipped — which is exactly
            // how it shipped.
            .safeDrawingPadding()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // No headline here: the activity's own title bar already says "Watch
        // settings", and repeating it cost a row of vertical space on a screen
        // that had to scroll to show three settings.
        Text(
            text = "These live on the watch. Change them here or there — both " +
                "update, and the most recent change wins.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        SettingCard {
            ToggleRow(
                title = "Raise to wake",
                subtitle = "Turn your wrist towards you to light the screen.",
                checked = settings.tiltEnabled,
                onCheckedChange = { on -> store.edit { it.copy(tiltEnabled = on) } },
            )
        }

        SettingCard {
            Text("Screen timeout", fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                "How long the display stays on before sleeping. Double-tap or a " +
                    "wrist raise wakes it again.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(10.dp))
            TimeoutChips(
                selected = settings.wakeSeconds,
                onSelect = { secs -> store.edit { it.copy(wakeSeconds = secs) } },
            )
            if (settings.wakeSeconds == 0) {
                Spacer(Modifier.height(8.dp))
                Text(
                    "Never: the screen stays on until you press the button. " +
                        "This flattens the battery in a few hours.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error,
                )
            }
        }

        SettingCard {
            ToggleRow(
                title = "Show step count",
                subtitle = "Display today's steps on the watch face.",
                checked = settings.showSteps,
                onCheckedChange = { on -> store.edit { it.copy(showSteps = on) } },
            )
        }

        SettingCard {
            ToggleRow(
                title = "Face diagnostics",
                subtitle = "Show the bring-up counter lines on the watch face.",
                checked = settings.showDiag,
                onCheckedChange = { on -> store.edit { it.copy(showDiag = on) } },
            )
        }

        Text(
            text = syncStatusLine(pending, settings.revision),
            style = MaterialTheme.typography.bodySmall,
            color = if (pending) {
                MaterialTheme.colorScheme.error
            } else {
                MaterialTheme.colorScheme.onSurfaceVariant
            },
            modifier = Modifier.padding(horizontal = 4.dp, vertical = 4.dp),
        )
    }
}

@Composable
private fun SettingCard(content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(modifier = Modifier.padding(16.dp)) { content() }
    }
}

/**
 * Label and switch on one line.
 *
 * The text is weighted rather than given a fraction of the width: a fixed 0.75f
 * left the switch floating in the middle on a wide screen and crowded it on a
 * narrow one.
 */
@Composable
private fun ToggleRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(title, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Spacer(Modifier.width(12.dp))
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

/**
 * The timeout choices, wrapping onto as many lines as they need.
 *
 * A plain Row could not fit six chips: "Never" was squeezed to one character
 * wide and rendered as a vertical column of letters. FlowRow wraps instead of
 * compressing, so the labels stay readable at any width or font scale.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TimeoutChips(selected: Int, onSelect: (Int) -> Unit) {
    FlowRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        WatchSettings.WAKE_SECONDS_CHOICES.forEach { secs ->
            FilterChip(
                selected = selected == secs,
                onClick = { onSelect(secs) },
                label = { Text(timeoutLabel(secs)) },
            )
        }
    }
}

private fun timeoutLabel(seconds: Int): String = when (seconds) {
    0 -> "Never"
    else -> "${seconds}s"
}

/**
 * Says whether the watch has this yet.
 *
 * Without it an edit made out of range looks identical to one that landed, and
 * the operator has no way to tell — the watch is sealed and its own screen is
 * the only other place these values show.
 */
private fun syncStatusLine(pending: Boolean, revision: Long): String =
    if (pending) {
        "Waiting for the watch — will send when it next connects (revision $revision)."
    } else {
        "In sync with the watch (revision $revision)."
    }
