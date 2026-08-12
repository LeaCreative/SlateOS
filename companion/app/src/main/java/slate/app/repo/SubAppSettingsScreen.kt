package slate.app.repo

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import slate.script.SubAppSetting

/**
 * Generated settings UI for one sub-app.
 *
 * The sub-app declares a schema in its manifest and the companion renders it —
 * a downloaded script never draws phone-side UI, and this way every app's
 * settings look and behave the same. See docs/subapp-rules.md §5.
 *
 * Values are written through [SubAppSetting.sanitise], so a value that reaches
 * storage is already clamped to the declared range. Scripts must still validate
 * on read: the store is shared with the script's own writes.
 */
@Composable
fun SubAppSettingsScreen(
    appName: String,
    appId: String,
    settings: List<SubAppSetting>,
    prefs: RepoPrefs,
    onClose: () -> Unit,
) {
    // Seeded once from prefs; edits live here until Save.
    val edited = remember(appId) {
        mutableStateMapOf<String, String>().apply {
            settings.forEach { s ->
                put(s.key, prefs.subAppSetting(appId, s.key) ?: s.defaultValue)
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
    ) {
        Text(text = appName, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text(text = appId, style = MaterialTheme.typography.bodySmall)
        Spacer(modifier = Modifier.height(12.dp))

        if (settings.isEmpty()) {
            Text("This sub-app declares no settings.")
        }

        settings.forEach { setting ->
            HorizontalDivider()
            Spacer(modifier = Modifier.height(8.dp))
            Text(text = setting.label, fontWeight = FontWeight.SemiBold)
            when (setting.type) {
                SubAppSetting.Type.BOOL -> {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(
                            checked = edited[setting.key] == "1",
                            onCheckedChange = { edited[setting.key] = if (it) "1" else "0" },
                        )
                        Text(if (edited[setting.key] == "1") "On" else "Off")
                    }
                }
                SubAppSetting.Type.CHOICE -> {
                    setting.options.forEach { opt ->
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Checkbox(
                                checked = edited[setting.key] == opt.value,
                                onCheckedChange = { edited[setting.key] = opt.value },
                            )
                            Text(opt.label)
                        }
                    }
                }
                SubAppSetting.Type.INT -> {
                    OutlinedTextField(
                        value = edited[setting.key] ?: "",
                        onValueChange = { edited[setting.key] = it.filter(Char::isDigit) },
                        label = {
                            Text(
                                buildString {
                                    append("Value")
                                    if (setting.unit.isNotBlank()) append(" (${setting.unit})")
                                    if (setting.min != null && setting.max != null) {
                                        append("  ${setting.min}–${setting.max}")
                                    }
                                },
                            )
                        },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
                SubAppSetting.Type.STRING -> {
                    OutlinedTextField(
                        value = edited[setting.key] ?: "",
                        onValueChange = {
                            val hi = setting.max ?: SubAppSetting.MAX_STRING_LEN
                            edited[setting.key] = it.take(hi.coerceIn(1, SubAppSetting.MAX_STRING_LEN))
                        },
                        label = { Text(if (setting.unit.isNotBlank()) setting.unit else "Value") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
            if (setting.help.isNotBlank()) {
                Text(text = setting.help, style = MaterialTheme.typography.bodySmall)
            }
            Spacer(modifier = Modifier.height(12.dp))
        }

        Row {
            Button(onClick = {
                // Sanitise on the way in, so out-of-range text never reaches the
                // script — it would otherwise arrive as a store string and the
                // script would have to defend against it alone.
                settings.forEach { s ->
                    prefs.setSubAppSetting(appId, s.key, s.sanitise(edited[s.key] ?: s.defaultValue))
                }
                onClose()
            }) { Text("Save") }
            Spacer(modifier = Modifier.height(8.dp))
            TextButton(onClick = onClose) { Text("Cancel") }
        }
        Text(
            text = "Takes effect the next time the sub-app is opened.",
            style = MaterialTheme.typography.bodySmall,
        )
    }
}
