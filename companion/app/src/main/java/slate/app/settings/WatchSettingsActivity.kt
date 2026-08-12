package slate.app.settings

import android.os.Bundle
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import slate.app.SlateActivity
import slate.app.theme.SlateTitleBar
import slate.app.theme.setSlateContent
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
class WatchSettingsActivity : SlateActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val store = WatchSettingsStore.get(this)
        setSlateContent {
            WatchSettingsScreen(store)
        }
    }
}

@Composable
fun WatchSettingsScreen(store: WatchSettingsStore) {
    // Collected, not remembered: the watch can change these while the screen is
    // open and the change must appear here without the user doing anything.
    val settings by store.settings.collectAsState()
    val pending by store.pendingSend.collectAsState()

    Scaffold(
        modifier = Modifier.fillMaxSize(),
        topBar = { SlateTitleBar(title = "Watch settings") },
    ) { innerPadding ->
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(innerPadding)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
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
            if (settings.tiltEnabled) {
                Spacer(Modifier.height(10.dp))
                Text("Raise sensitivity", fontWeight = FontWeight.Medium)
                Spacer(Modifier.height(2.dp))
                Text(
                    "Soft wakes on a smaller wrist roll; Hard needs a clearer raise.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(8.dp))
                SensitivityChips(
                    selected = settings.raiseSensitivity,
                    onSelect = { s -> store.edit { it.copy(raiseSensitivity = s) } },
                )
            }
        }

        SettingCard {
            ToggleRow(
                title = "Shake to wake",
                subtitle = "A wrist flick or jolt lights the screen " +
                    "(InfiniTime-style; separate from raise).",
                checked = settings.shakeEnabled,
                onCheckedChange = { on -> store.edit { it.copy(shakeEnabled = on) } },
            )
            if (settings.shakeEnabled) {
                Spacer(Modifier.height(10.dp))
                Text("Shake sensitivity", fontWeight = FontWeight.Medium)
                Spacer(Modifier.height(2.dp))
                Text(
                    "Soft wakes on a lighter flick; Hard needs a sharper shake.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(8.dp))
                SensitivityChips(
                    selected = settings.shakeSensitivity,
                    onSelect = { s -> store.edit { it.copy(shakeSensitivity = s) } },
                )
            }
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

        SettingCard {
            ToggleRow(
                title = "Heart rate",
                subtitle = "Turn on the optical sensor. BPM shows on the watch " +
                    "face next to steps. Uses several mA while on — leave Off " +
                    "when you do not need it.",
                checked = settings.hrEnabled,
                onCheckedChange = { on -> store.edit { it.copy(hrEnabled = on) } },
            )
        }

        SettingCard {
            Text("Appearance", fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(
                "Colours sync to the watch. Status glyphs and the OTA banner " +
                    "stay green/amber.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(12.dp))
            ThemeColorRow(
                title = "Buttons and Text",
                subtitle = "Outlines and text on Settings, Notifications, Apps",
                rgb565 = settings.uiChrome,
                onPick = { c -> store.edit { it.copy(uiChrome = c) } },
            )
            Spacer(Modifier.height(10.dp))
            ThemeColorRow(
                title = "Face bright",
                subtitle = "Time digits and battery fill",
                rgb565 = settings.faceBright,
                onPick = { c -> store.edit { it.copy(faceBright = c) } },
            )
            Spacer(Modifier.height(10.dp))
            ThemeColorRow(
                title = "Face dim",
                subtitle = "Date, steps, HR, battery %, track, diagnostics",
                rgb565 = settings.faceDim,
                onPick = { c -> store.edit { it.copy(faceDim = c) } },
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

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ThemeColorRow(
    title: String,
    subtitle: String,
    rgb565: Int,
    onPick: (Int) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(title, fontWeight = FontWeight.Medium)
            Text(
                subtitle,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Box(
            modifier = Modifier
                .size(40.dp)
                .border(1.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(8.dp))
                .background(
                    Color(WatchUiTheme.rgb565ToArgb(rgb565)),
                    RoundedCornerShape(8.dp),
                )
                .clickable { open = true },
        )
    }
    if (open) {
        AlertDialog(
            onDismissRequest = { open = false },
            title = { Text(title) },
            text = {
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    THEME_PRESETS.forEach { preset ->
                        Box(
                            modifier = Modifier
                                .size(36.dp)
                                .border(
                                    width = if (preset == rgb565) 2.dp else 1.dp,
                                    color = if (preset == rgb565) {
                                        MaterialTheme.colorScheme.primary
                                    } else {
                                        MaterialTheme.colorScheme.outline
                                    },
                                    shape = RoundedCornerShape(6.dp),
                                )
                                .background(
                                    Color(WatchUiTheme.rgb565ToArgb(preset)),
                                    RoundedCornerShape(6.dp),
                                )
                                .clickable {
                                    onPick(preset)
                                    open = false
                                },
                        )
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { open = false }) { Text("Close") }
            },
        )
    }
}

/** RGB565 presets: greys unchanged, then three evenly spaced value steps per hue. */
private val THEME_PRESETS: List<Int> = listOf(
    // Greyscale (unchanged)
    0xFFFF, // white
    0xC618, // light grey
    0x8410, // mid grey (default face dim)
    0x4208, // dark grey
    // Red — V = 1, ⅔, ⅓
    0xF800,
    0xA000,
    0x5000,
    // Orange
    0xFBE0,
    0xA2A0,
    0x5140,
    // Yellow
    0xFFE0,
    0xA540,
    0x52A0,
    // Green
    0x07E0,
    0x0540,
    0x02A0,
    // Cyan
    0x07FF,
    0x0554,
    0x02AA,
    // Blue
    0x001F,
    0x0014,
    0x000A,
    // Magenta
    0xF81F,
    0xA014,
    0x500A,
)

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

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SensitivityChips(selected: Int, onSelect: (Int) -> Unit) {
    FlowRow(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        WatchSettings.SENSITIVITY_CHOICES.forEach { level ->
            FilterChip(
                selected = WatchSettings.clampSens(selected) == level,
                onClick = { onSelect(level) },
                label = { Text(WatchSettings.sensitivityLabel(level)) },
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
