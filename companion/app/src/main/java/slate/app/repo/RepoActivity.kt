package slate.app.repo

import android.os.Bundle
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import slate.app.SlateActivity
import slate.app.link.LinkForegroundService
import slate.app.theme.SlateTitleBar
import slate.app.theme.setSlateContent
import slate.repo.Availability
import slate.repo.PermissionPolicy
import slate.repo.RepoTrust
import slate.script.ScriptPermission
import slate.script.SubAppSetting

class RepoActivity : SlateActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = RepoPrefs(this)
        val store = InstalledStore.create(this)
        val hostVer = packageManager.getPackageInfo(packageName, 0).versionName ?: "0.1.0"
        val manager = RepoManager(
            context = this,
            prefs = prefs,
            store = store,
            hostVersion = hostVer,
            watchProtocol = {
                LinkForegroundService.instance?.watchProtocolVersion() ?: 1
            },
        )
        setSlateContent {
            RepoScreen(manager = manager, prefs = prefs, context = this)
        }
    }
}

private enum class Tab { Browse, Installed, Sources }

@Composable
private fun RepoScreen(manager: RepoManager, prefs: RepoPrefs, context: android.content.Context) {
    var tab by remember { mutableStateOf(Tab.Browse) }
    var selected by remember { mutableStateOf<BrowseItem?>(null) }
    val catalog by manager.catalog.collectAsState()
    // Which app ids are kept off the watch's launcher. Seeded once and updated
    // in place so a tick redraws immediately rather than after a refresh.
    val launcherHidden = remember { mutableStateOf(prefs.hiddenFromLauncher()) }
    // Non-null while a sub-app's generated settings screen is open.
    var settingsFor by remember { mutableStateOf<BrowseItem?>(null) }
    val status by manager.status.collectAsState()
    val scope = rememberCoroutineScope()

    LaunchedEffect(Unit) {
        manager.refreshLocal()
        manager.refreshIndexes()
    }

    val current = selected
    Scaffold(
        modifier = Modifier.fillMaxSize(),
        topBar = { SlateTitleBar(title = "Sub-app repository") },
    ) { innerPadding ->
    if (current != null) {
        DetailScreen(
            item = current,
            prefs = prefs,
            onBack = { selected = null },
            onInstall = { item, grants ->
                scope.launch {
                    manager.install(item, grants, userConfirmedPerms = true)
                    selected = manager.catalog.value.firstOrNull { it.entry.app.id == item.entry.app.id }
                }
            },
            onUninstall = { id ->
                manager.uninstall(id)
                selected = null
            },
            modifier = Modifier.padding(innerPadding),
        )
        return@Scaffold
    }

    Column(modifier = Modifier.padding(innerPadding).padding(16.dp)) {
        Text(text = status, style = MaterialTheme.typography.bodySmall)
        Spacer(modifier = Modifier.height(8.dp))
        Row(
            modifier = Modifier.horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            val chipShape = RoundedCornerShape(12.dp)
            OutlinedButton(onClick = { tab = Tab.Browse }, shape = chipShape) { Text("Browse") }
            OutlinedButton(onClick = { tab = Tab.Installed }, shape = chipShape) { Text("Installed") }
            OutlinedButton(onClick = { tab = Tab.Sources }, shape = chipShape) { Text("Sources") }
            OutlinedButton(
                onClick = { scope.launch { manager.refreshIndexes(force = true) } },
                shape = chipShape,
            ) { Text("Refresh") }
            OutlinedButton(
                onClick = {
                    BundledPackageSeeder.ensureOfficialDemos(context)
                    manager.refreshLocal()
                },
                shape = chipShape,
            ) { Text("Seed demos") }
        }
        Spacer(modifier = Modifier.height(8.dp))
        settingsFor?.let { item ->
            val inst = item.installed
            if (inst != null) {
                AlertDialog(
                    onDismissRequest = { settingsFor = null },
                    confirmButton = {},
                    text = {
                        SubAppSettingsScreen(
                            appName = item.entry.app.name,
                            appId = item.entry.app.id,
                            settings = declaredSettings(inst),
                            prefs = prefs,
                            onClose = { settingsFor = null },
                        )
                    },
                )
            }
        }

        when (tab) {
            Tab.Browse -> LazyColumn {
                items(catalog, key = { it.entry.app.id + "/" + it.entry.repoId }) { item ->
                    LauncherToggleRow(
                        item, prefs, launcherHidden,
                        onClick = { selected = item },
                        onOpenSettings = { settingsFor = item },
                    )
                    HorizontalDivider()
                }
            }
            Tab.Installed -> LazyColumn {
                items(
                    catalog.filter { it.installed != null },
                    key = { it.entry.app.id },
                ) { item ->
                    LauncherToggleRow(
                        item, prefs, launcherHidden,
                        onClick = { selected = item },
                        onOpenSettings = { settingsFor = item },
                    )
                    HorizontalDivider()
                }
            }
            Tab.Sources -> SourcesPanel(prefs = prefs, manager = manager)
        }
    }
    }
}

/**
 * A catalogue row with its launcher checkbox wired to [RepoPrefs].
 *
 * [hidden] is passed in rather than read here so ticking one box recomposes
 * every row from the same source of truth — the set is what the launcher
 * itself filters on, and the two must not drift apart.
 */
/** Settings a sub-app declares, read from its installed manifest. */
private fun declaredSettings(installed: InstalledStore.InstalledApp): List<SubAppSetting> =
    try {
        SubAppSetting.parseAll(
            java.io.File(installed.dir, "manifest.json").readText(Charsets.UTF_8),
        )
    } catch (_: Throwable) {
        emptyList()
    }

@Composable
private fun LauncherToggleRow(
    item: BrowseItem,
    prefs: RepoPrefs,
    hidden: MutableState<Set<String>>,
    onClick: () -> Unit,
    onOpenSettings: () -> Unit = {},
) {
    val id = item.entry.app.id
    // The gear only appears when the sub-app actually declares settings, so an
    // app with none never shows a control that would open an empty screen.
    val hasSettings = item.installed?.let { declaredSettings(it).isNotEmpty() } ?: false
    CatalogRow(
        item = item,
        onClick = onClick,
        showsInLauncher = if (item.installed != null) id !in hidden.value else null,
        onShowsInLauncherChange = { show ->
            prefs.setShowsInLauncher(id, show)
            hidden.value = prefs.hiddenFromLauncher()
        },
        onOpenSettings = if (hasSettings) onOpenSettings else null,
    )
}

@Composable
private fun CatalogRow(
    item: BrowseItem,
    onClick: () -> Unit,
    /** Null when the app is not installed — it cannot be on the watch anyway. */
    showsInLauncher: Boolean? = null,
    onShowsInLauncherChange: (Boolean) -> Unit = {},
    /** Null when the sub-app declares no settings — no gear is drawn. */
    onOpenSettings: (() -> Unit)? = null,
) {
    val app = item.entry.app
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(vertical = 10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (showsInLauncher != null) {
                // Whether this sub-app appears in the watch's launcher. Outside
                // the row's clickable area, so ticking it does not also open
                // the detail sheet.
                Checkbox(
                    checked = showsInLauncher,
                    onCheckedChange = onShowsInLauncherChange,
                )
            }
            Text(
                text = app.name,
                fontWeight = FontWeight.SemiBold,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = if (item.entry.trust == RepoTrust.Official) {
                    "Official"
                } else {
                    item.entry.repoName
                },
                style = MaterialTheme.typography.labelMedium,
            )
            if (onOpenSettings != null) {
                // Outside the row's clickable area, like the launcher checkbox,
                // so opening settings does not also open the detail sheet.
                TextButton(onClick = onOpenSettings) { Text("\u2699") }
            }
        }
        Text(
            text = "${app.id} · v${app.version}",
            style = MaterialTheme.typography.bodySmall,
        )
        when (val a = item.availability) {
            is Availability.Available -> {
                val tag = when {
                    item.installed == null -> "Not installed"
                    item.updateNeedsConsent -> "Update needs permission consent"
                    item.installed.version != app.version -> "Update available"
                    else -> "Installed ${item.installed.version}"
                }
                Text(text = tag, style = MaterialTheme.typography.bodySmall)
            }
            is Availability.Unavailable -> Text(
                text = "Unavailable: ${a.reason}",
                color = MaterialTheme.colorScheme.error,
            )
        }
    }
}

@Composable
private fun DetailScreen(
    item: BrowseItem,
    prefs: RepoPrefs,
    onBack: () -> Unit,
    onInstall: (BrowseItem, Set<ScriptPermission>) -> Unit,
    onUninstall: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val app = item.entry.app
    var confirmInstall by remember { mutableStateOf(false) }
    var grants by remember {
        mutableStateOf(prefs.userGrantedSensitive(app.id))
    }

    Column(
        modifier = modifier
            .padding(16.dp)
            .verticalScroll(rememberScrollState()),
    ) {
        TextButton(onClick = onBack) { Text("Back") }
        Text(
            text = app.name,
            style = MaterialTheme.typography.headlineSmall,
            fontWeight = FontWeight.Bold,
        )
        Text(
            text = "From ${item.entry.repoName} (${item.entry.trust.name})",
            style = MaterialTheme.typography.bodyMedium,
        )
        Text(text = app.author.ifBlank { "-" })
        Text(text = app.description.ifBlank { "No description." })
        Spacer(modifier = Modifier.height(8.dp))
        Text(text = "Permissions declared", fontWeight = FontWeight.SemiBold)
        if (app.permissions.isEmpty()) {
            Text(text = "(none)")
        } else {
            for (p in app.permissions) {
                Text(text = "• ${p.id}")
            }
        }
        if (item.entry.trust == RepoTrust.ThirdParty) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "Third-party repos block http / location / health.read unless you grant them below.",
                style = MaterialTheme.typography.bodySmall,
            )
            for (p in PermissionPolicy.THIRD_PARTY_BLOCKED.intersect(app.permissions)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(
                        checked = p in grants,
                        onCheckedChange = { checked ->
                            grants = if (checked) grants + p else grants - p
                        },
                    )
                    Text(text = "Allow ${p.id}")
                }
            }
        }
        val avail = item.availability
        if (avail is Availability.Unavailable) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(text = avail.reason, color = MaterialTheme.colorScheme.error)
        }
        if (app.screenshots.isNotEmpty()) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "Screenshots: ${app.screenshots.size} (URLs in index)",
                style = MaterialTheme.typography.bodySmall,
            )
            for (shot in app.screenshots.take(3)) {
                Text(text = shot, style = MaterialTheme.typography.labelSmall)
            }
        }
        Spacer(modifier = Modifier.height(16.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = { confirmInstall = true }) {
                Text(text = if (item.installed == null) "Install..." else "Update...")
            }
            if (item.installed != null) {
                Button(onClick = { onUninstall(app.id) }) { Text("Remove") }
            }
        }
        if (item.updateNeedsConsent) {
            Text(
                text = "This update adds permissions — auto-update is blocked until you confirm.",
                color = MaterialTheme.colorScheme.error,
            )
        }
    }

    if (confirmInstall) {
        val blocked =
            PermissionPolicy.blockedByDefault(app.permissions, item.entry.trust) - grants
        AlertDialog(
            onDismissRequest = { confirmInstall = false },
            title = { Text("Allow permissions?") },
            text = {
                Column {
                    Text("This sub-app asks for:")
                    for (p in app.permissions) {
                        Text("• ${p.id}")
                    }
                    if (blocked.isNotEmpty()) {
                        Text("Still blocked (third-party default): ${blocked.joinToString { it.id }}")
                    }
                    Text("Install uses the network once. Running later is offline from the local cache.")
                }
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        confirmInstall = false
                        onInstall(item, grants)
                    },
                ) { Text("Install") }
            },
            dismissButton = {
                TextButton(onClick = { confirmInstall = false }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun SourcesPanel(prefs: RepoPrefs, manager: RepoManager) {
    var name by remember { mutableStateOf("") }
    var url by remember { mutableStateOf("") }
    var pubKey by remember { mutableStateOf("") }
    var metered by remember { mutableStateOf(prefs.allowMeteredUpdates) }
    var auto by remember { mutableStateOf(prefs.autoUpdateEnabled) }
    val scope = rememberCoroutineScope()
    val sources = prefs.sources()

    Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Checkbox(
                checked = metered,
                onCheckedChange = {
                    metered = it
                    prefs.allowMeteredUpdates = it
                },
            )
            Text("Allow index/install on metered networks")
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Checkbox(
                checked = auto,
                onCheckedChange = {
                    auto = it
                    prefs.autoUpdateEnabled = it
                },
            )
            Text("Auto-update when permissions do not increase")
        }
        Spacer(modifier = Modifier.height(8.dp))
        Text(text = "Configured sources", fontWeight = FontWeight.SemiBold)
        for (s in sources) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("${s.name} (${s.trust.name})")
                    Text(text = s.indexUrl, style = MaterialTheme.typography.labelSmall)
                }
                if (s.trust == RepoTrust.ThirdParty) {
                    TextButton(
                        onClick = {
                            prefs.removeThirdParty(s.id)
                            scope.launch { manager.refreshIndexes(force = true) }
                        },
                    ) { Text("Remove") }
                }
            }
        }
        Spacer(modifier = Modifier.height(12.dp))
        Text(text = "Add third-party repository", fontWeight = FontWeight.SemiBold)
        Text(
            text = "HTTPS index + Ed25519 public key. Reduced permissions. Cannot shadow official app IDs.",
            style = MaterialTheme.typography.bodySmall,
        )
        OutlinedTextField(
            value = name,
            onValueChange = { name = it },
            label = { Text("Name") },
            modifier = Modifier.fillMaxWidth(),
        )
        OutlinedTextField(
            value = url,
            onValueChange = { url = it },
            label = { Text("HTTPS index URL") },
            modifier = Modifier.fillMaxWidth(),
        )
        OutlinedTextField(
            value = pubKey,
            onValueChange = { pubKey = it },
            label = { Text("Ed25519 public key (SPKI base64)") },
            modifier = Modifier.fillMaxWidth(),
        )
        Button(
            onClick = {
                if (prefs.addThirdParty(name, url, pubKey.ifBlank { null })) {
                    name = ""
                    url = ""
                    pubKey = ""
                    scope.launch { manager.refreshIndexes(force = true) }
                }
            },
        ) { Text("Add repository") }
    }
}
