package slate.app.repo

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import slate.repo.AppAvailability
import slate.repo.Availability
import slate.repo.CatalogEntry
import slate.repo.CatalogMerge
import slate.repo.Digests
import slate.repo.Ed25519IndexVerifier
import slate.repo.HostVersion
import slate.repo.IndexApp
import slate.repo.IndexParser
import slate.repo.PermissionPolicy
import slate.repo.RepoTrust
import slate.repo.SlatePackageReader
import slate.repo.UpdatePolicy
import slate.script.ScriptPermission
import java.io.File

data class BrowseItem(
    val entry: CatalogEntry,
    val availability: Availability,
    val installed: InstalledStore.InstalledApp?,
    val blockedSensitive: Set<ScriptPermission>,
    val updateNeedsConsent: Boolean,
)

/**
 * Fetch / verify / install orchestrator. Installing needs network; running does not.
 */
class RepoManager(
    private val context: Context,
    private val prefs: RepoPrefs,
    private val store: InstalledStore,
    private val hostVersion: String,
    private val watchProtocol: () -> Int,
) {
    private val _catalog = MutableStateFlow<List<BrowseItem>>(emptyList())
    val catalog: StateFlow<List<BrowseItem>> = _catalog.asStateFlow()

    private val _status = MutableStateFlow("")
    val status: StateFlow<String> = _status.asStateFlow()

    private val indexCacheDir = File(context.cacheDir, "repo-index").also { it.mkdirs() }

    /**
     * Re-stamp installed status, and make sure everything actually installed
     * is in the catalogue.
     *
     * This used to bail out after the re-stamp whenever the catalogue was
     * non-empty, so an installed package that no index happened to list was
     * invisible — including on the Installed tab, which filters this same
     * catalogue. A bundled demo seeded straight into the store (Buzz Phone was
     * the first) simply never appeared, while being present in
     * `files/repo/installed.json` and perfectly launchable from the watch.
     *
     * Index entries win where the ids collide: they carry the real repo
     * identity, signature and update URL. The store only fills the gaps.
     */
    fun refreshLocal() {
        val current = _catalog.value.map { it.copy(installed = store.get(it.entry.app.id)) }
        val known = current.map { it.entry.app.id }.toSet()
        val extras = store.list()
            .filter { it.id !in known }
            .map { localBrowseItem(it) }
        _catalog.value = current + extras
    }

    private fun localBrowseItem(inst: InstalledStore.InstalledApp): BrowseItem {
        val m = inst.manifest()
        return run {
            BrowseItem(
                entry = CatalogEntry(
                    app = IndexApp(
                        id = inst.id,
                        version = inst.version,
                        name = inst.name,
                        description = m.description,
                        author = m.author,
                        minProtocolVersion = m.minProtocolVersion,
                        minHostVersion = m.minHostVersion,
                        permissions = inst.permissions,
                        sha256 = inst.sha256,
                        url = "https://local.invalid/${inst.id}",
                    ),
                    repoId = inst.repoId,
                    repoName = if (inst.trust == RepoTrust.Official) "Official" else inst.repoId,
                    trust = inst.trust,
                    indexUrl = "",
                ),
                availability = AppAvailability.check(
                    m.minProtocolVersion,
                    m.minHostVersion,
                    watchProtocol(),
                    hostVersion,
                ),
                installed = inst,
                blockedSensitive = emptySet(),
                updateNeedsConsent = false,
            )
        }
    }


    suspend fun refreshIndexes(force: Boolean = false): Result<Unit> {
        if (!force && isMetered() && !prefs.allowMeteredUpdates) {
            _status.value = "On metered network — enable “Allow updates on metered” or use Wi‑Fi"
            refreshLocal()
            return Result.failure(RepoHttpException("metered"))
        }
        return try {
            val entries = ArrayList<CatalogEntry>()
            for (src in prefs.sources()) {
                val cachedJson = File(indexCacheDir, "${src.id}.json")
                val cachedSig = File(indexCacheDir, "${src.id}.sig")
                val indexBytes = try {
                    RepoHttp.getBytes(src.indexUrl)
                } catch (t: Throwable) {
                    if (cachedJson.isFile) cachedJson.readBytes() else throw t
                }
                val key = src.publicKeySpkiBase64
                if (key.isNullOrBlank()) {
                    _status.value = "Repo ${src.name}: missing public key — skipped"
                    continue
                }
                val sigUrl = signatureUrl(src.indexUrl)
                val sig = try {
                    RepoHttp.getText(sigUrl)
                } catch (_: Throwable) {
                    if (cachedSig.isFile) cachedSig.readText() else {
                        _status.value = "Repo ${src.name}: signature missing — skipped"
                        continue
                    }
                }
                if (!Ed25519IndexVerifier.verify(indexBytes, sig, key)) {
                    _status.value = "Repo ${src.name}: Ed25519 verify failed — skipped"
                    continue
                }
                cachedJson.writeBytes(indexBytes)
                cachedSig.writeText(sig.trim())
                val index = IndexParser.parse(indexBytes.toString(Charsets.UTF_8))
                for (app in index.apps) {
                    entries += CatalogEntry(
                        app = app,
                        repoId = src.id,
                        repoName = src.name,
                        trust = src.trust,
                        indexUrl = src.indexUrl,
                    )
                }
            }
            val merged = CatalogMerge.merge(entries)
            _status.value = if (merged.shadowed.isNotEmpty()) {
                "Hid ${merged.shadowed.size} third-party app(s) shadowing official IDs"
            } else {
                "Updated ${merged.kept.size} apps"
            }
            _catalog.value = merged.kept.map { toBrowse(it) }
            maybeAutoUpdate()
            Result.success(Unit)
        } catch (t: Throwable) {
            _status.value = "Refresh failed: ${t.message}"
            refreshLocal()
            Result.failure(t)
        }
    }

    private fun signatureUrl(indexUrl: String): String =
        if (indexUrl.endsWith(".json")) "$indexUrl.sig" else "${indexUrl.trimEnd('/')}/index.json.sig"

    private fun toBrowse(e: CatalogEntry): BrowseItem {
        val installed = store.get(e.app.id)
        val avail = AppAvailability.check(
            e.app.minProtocolVersion,
            e.app.minHostVersion,
            watchProtocol(),
            hostVersion,
        )
        val blocked = PermissionPolicy.blockedByDefault(e.app.permissions, e.trust)
        val consent = if (installed != null &&
            HostVersion.compare(e.app.version, installed.version) > 0
        ) {
            !UpdatePolicy.mayAutoInstall(installed.permissions, e.app.permissions)
        } else {
            false
        }
        return BrowseItem(
            entry = e,
            availability = avail,
            installed = installed,
            blockedSensitive = blocked - prefs.userGrantedSensitive(e.app.id),
            updateNeedsConsent = consent,
        )
    }

    private suspend fun maybeAutoUpdate() {
        if (!prefs.autoUpdateEnabled) return
        if (isMetered() && !prefs.allowMeteredUpdates) return
        for (item in _catalog.value) {
            val inst = item.installed ?: continue
            if (item.availability !is Availability.Available) continue
            if (HostVersion.compare(item.entry.app.version, inst.version) <= 0) continue
            if (item.updateNeedsConsent) continue
            if (!UpdatePolicy.mayAutoInstall(inst.permissions, item.entry.app.permissions)) continue
            try {
                install(
                    item,
                    grantSensitive = prefs.userGrantedSensitive(item.entry.app.id),
                    userConfirmedPerms = true,
                )
            } catch (_: Throwable) {
            }
        }
    }

    /**
     * Download + verify SHA-256 + validate manifest + write local cache.
     * [userConfirmedPerms] must be true after the permission disclosure UI.
     */
    suspend fun install(
        item: BrowseItem,
        grantSensitive: Set<ScriptPermission>,
        userConfirmedPerms: Boolean,
    ): Result<Unit> {
        if (!userConfirmedPerms) {
            return Result.failure(IllegalStateException("permission disclosure required"))
        }
        return try {
            val bytes = RepoHttp.getBytes(item.entry.app.url)
            if (!Digests.matches(bytes, item.entry.app.sha256)) {
                return Result.failure(IllegalStateException("SHA-256 mismatch"))
            }
            val pkg = SlatePackageReader.open(bytes, item.entry.app.sha256)
            if (pkg.manifest.id != item.entry.app.id) {
                return Result.failure(IllegalStateException("package id != index id"))
            }
            prefs.setUserGrantedSensitive(item.entry.app.id, grantSensitive)
            val effective = PermissionPolicy.effective(
                declared = pkg.manifest.permissions,
                trust = item.entry.trust,
                userGrantedSensitive = grantSensitive,
            )
            store.install(
                packageFiles = pkg.files,
                manifest = pkg.manifest,
                repoId = item.entry.repoId,
                trust = item.entry.trust,
                sha256 = pkg.sha256,
                effectivePermissions = effective,
            )
            _catalog.value = _catalog.value.map {
                if (it.entry.app.id == item.entry.app.id) toBrowse(it.entry) else it
            }
            _status.value = "Installed ${pkg.manifest.name} ${pkg.manifest.version}"
            Result.success(Unit)
        } catch (t: Throwable) {
            _status.value = "Install failed: ${t.message}"
            Result.failure(t)
        }
    }

    fun uninstall(appId: String) {
        store.remove(appId)
        _catalog.value = _catalog.value.map {
            if (it.entry.app.id == appId) {
                it.copy(installed = null, updateNeedsConsent = false)
            } else {
                it
            }
        }
        _status.value = "Removed $appId"
    }

    private fun isMetered(): Boolean {
        val cm = context.getSystemService(ConnectivityManager::class.java) ?: return false
        val net = cm.activeNetwork ?: return false
        val caps = cm.getNetworkCapabilities(net) ?: return false
        return !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
    }
}
