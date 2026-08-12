package slate.app.script

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.app.repo.BundledPackageSeeder
import java.io.File
import slate.script.SubAppSetting
import slate.app.repo.InstalledStore
import slate.app.repo.RepoPrefs
import slate.compositor.Compositor
import slate.repo.PermissionPolicy
import slate.script.JsSlateAppEndpoint
import slate.script.ScriptConsole
import slate.script.ScriptResources

/**
 * Owns JS sub-app sessions loaded from the M13 [InstalledStore]
 * (seeded official demos or repo-installed packages).
 */
class ScriptRuntimeHost(
    private val context: Context,
    private val scope: CoroutineScope,
    private val compositor: Compositor,
) {
    private val apps = HashMap<String, JsSlateAppEndpoint>()
    /** sha256|trust|version at last successful register — detect sideload mid-session. */
    private val registeredFingerprint = HashMap<String, String>()
    private val timerJobs = HashMap<String, Job>()
    private val prefs by lazy { RepoPrefs(context) }
    var lastRenderIpcMs: Long = -1L
        private set

    suspend fun ensureSeeded() {
        BundledPackageSeeder.ensureOfficialDemos(context)
    }

    suspend fun ensureTimerRegistered() = ensureRegistered(TIMER_ID)

    suspend fun ensureRegistered(appId: String) {
        ensureSeeded()
        val installed = InstalledStore.create(context).get(appId)
            ?: error("Package $appId not installed — open Sub-app repository or re-seed demos")
        val fingerprint = "${installed.sha256}|${installed.trust}|${installed.version}"
        val already = apps[appId]
        if (already != null) {
            if (registeredFingerprint[appId] == fingerprint) {
                // Re-seed the declared settings even though the app is already
                // running. This used to return immediately, which meant settings
                // were read exactly once per service lifetime: a user could change
                // the map radius or the timer duration, reopen the app, and see no
                // difference at all until something restarted the link service.
                // §5.2 promises the script that settings are read at focus, and
                // this is the only place that promise can be kept.
                val settings = settingsFor(appId, installed)
                if (settings.isNotEmpty()) {
                    already.seedSettings(settings)
                    ScriptConsole.log(
                        appId,
                        "info",
                        "settings refreshed: " + settings.entries.joinToString { "${it.key}=${it.value}" },
                    )
                }
                return
            }
            ScriptConsole.log(appId, "info", "re-registering after package change")
            LinkLog.i("$appId: re-registering after package change")
            dropRegistered(appId)
        }
        if (apps.isEmpty() && lastRenderIpcMs < 0) {
            probeIpc()
        }
        val declared = installed.manifest().permissions
        val granted = PermissionPolicy.bindable(
            declared = declared,
            trust = installed.trust,
            userGrantedSensitive = prefs.userGrantedSensitive(appId),
        )
        val denied = declared - granted
        ScriptConsole.log(
            appId,
            "info",
            "permissions requested=[${declared.joinToString { it.id }}] " +
                "granted=[${granted.joinToString { it.id }}]" +
                if (denied.isEmpty()) {
                    ""
                } else {
                    " denied=[${denied.joinToString { it.id }}]"
                },
        )
        if (denied.isNotEmpty()) {
            LinkLog.w(
                "$appId: permissions denied=[${denied.joinToString { it.id }}] " +
                    "(trust=${installed.trust})",
            )
        }
        val script = installed.manifest().toScriptManifest()
            .copy(permissions = granted)
        val engine = AndroidJsEngine.create(context)
        val ceiling = PermissionPolicy.sourceCeiling(installed.trust)
        val ep = JsSlateAppEndpoint(
            scriptManifest = script,
            engine = engine,
            hostHeldPermissions = PermissionPolicy.HOST_HELD,
            sourceCeiling = ceiling,
            onTimerSet = { id, ms -> scheduleTimer(appId, id, ms) },
            onTimerClear = { id -> clearTimer(id) },
            initialStore = settingsFor(appId, installed),
            onAdapterDenied = { adapter, command ->
                LinkLog.w("$appId: adapter denied $adapter.$command (missing permission)")
                val detail = "permission denied"
                scope.launch {
                    when {
                        adapter == "news" && command == "list" ->
                            compositor.dispatchSystemEvent(
                                appId,
                                "news",
                                JSONObject()
                                    .put("type", "status")
                                    .put("state", "error")
                                    .put("detail", detail)
                                    .toString(),
                            )
                        adapter == "http" ->
                            compositor.dispatchSystemEvent(
                                appId,
                                "http",
                                JSONObject()
                                    .put("type", "error")
                                    .put("id", "")
                                    .put("detail", detail)
                                    .toString(),
                            )
                        adapter == "media" && command == "subscribe" ->
                            compositor.dispatchSystemEvent(
                                appId,
                                "media",
                                JSONObject()
                                    .put("type", "status")
                                    .put("state", "error")
                                    .put("detail", detail)
                                    .toString(),
                            )
                        adapter == "weather" && command == "fetch" ->
                            compositor.dispatchSystemEvent(
                                appId,
                                "weather",
                                JSONObject()
                                    .put("type", "status")
                                    .put("state", "error")
                                    .put("detail", detail)
                                    .toString(),
                            )
                    }
                }
            },
        )
        ep.installRuntime(appJs = installed.entryJs())
        compositor.register(ep)
        apps[appId] = ep
        registeredFingerprint[appId] = fingerprint
        ScriptConsole.log(appId, "info", "registered from InstalledStore v${installed.version}")
    }

    private fun dropRegistered(appId: String) {
        apps.remove(appId)?.close()
        compositor.unregister(appId)
        registeredFingerprint.remove(appId)
    }

    /**
     * Declared settings as store entries: the user's value where they set one,
     * the manifest default otherwise.
     *
     * Read at registration, so a change takes effect the next time the sub-app
     * is opened rather than mid-session — which is what docs/subapp-rules.md
     * §5.2 tells script authors to expect.
     */
    private fun settingsFor(
        appId: String,
        installed: InstalledStore.InstalledApp,
    ): Map<String, String> {
        val declared = try {
            SubAppSetting.parseAll(
                File(installed.dir, "manifest.json").readText(Charsets.UTF_8),
            )
        } catch (_: Throwable) {
            emptyList()
        }
        if (declared.isEmpty()) return emptyMap()
        val prefs = RepoPrefs(context)
        return declared.associate { setting ->
            val raw = prefs.subAppSetting(appId, setting.key) ?: setting.defaultValue
            setting.key to setting.sanitise(raw)
        }
    }

    private suspend fun probeIpc() {
        val engine = AndroidJsEngine.create(context)
        engine.evaluate(ScriptResources.read(ScriptResources.UI_JS))
        engine.evaluate(ScriptResources.read(ScriptResources.HOST_JS))
        engine.evaluate("function render(){ return []; }")
        var ipcTotal = 0L
        repeat(5) {
            val json = """{"type":"render"}"""
            engine.evaluate("""__slate_dispatch('$json')""")
            ipcTotal += engine.lastEvalMs
        }
        val ipc = ipcTotal / 5
        lastRenderIpcMs = ipc
        ScriptConsole.ipc("slate.runtime", "render() isolate round-trip (empty)", ipc)
        if (ipc > 20L) {
            ScriptConsole.log(
                "slate.runtime",
                "warn",
                "IPC render ${ipc}ms > 20ms — propose QuickJS in-process fallback with the same whitelisted BindingSurface (no reflection)",
            )
        }
        engine.close()
    }

    private fun scheduleTimer(appId: String, id: String, intervalMs: Long) {
        clearTimer(id)
        timerJobs[id] = scope.launch {
            while (isActive) {
                delay(intervalMs)
                compositor.dispatchSystemEvent(appId, "timer", JSONObject().put("id", id).toString())
            }
        }
    }

    private fun clearTimer(id: String) {
        timerJobs.remove(id)?.cancel()
    }

    fun close() {
        timerJobs.values.forEach { it.cancel() }
        timerJobs.clear()
        apps.values.forEach { it.close() }
        apps.clear()
        registeredFingerprint.clear()
    }

    /**
     * Drop every running JS endpoint and the shared sandbox host.
     *
     * Used when create() fails with the N-51 "bound but unreachable" brick so
     * the next launch can rebind without force-stopping the whole companion.
     * Also safe on link-service destroy.
     */
    suspend fun resetRuntime() {
        close()
        lastRenderIpcMs = -1L
        AndroidJsEngine.forceReset()
    }

    /** Forget one app so [ensureRegistered] rebuilds it with a fresh isolate. */
    fun evict(appId: String) {
        dropRegistered(appId)
    }

    companion object {
        const val TIMER_ID = "slate.timer"
        const val NAV_ID = "slate.navigation"
        const val CAMERA_ID = "slate.camera"
        const val VIBRATE_ID = "slate.vibrate"
        const val LOCATION_ID = "slate.location"
        const val MAP_ID = "slate.map"
        const val NEWS_ID = "slate.news"
        const val MEDIA_ID = "slate.media"
        const val WEATHER_ID = "slate.weather"
        const val HTTPDEMO_ID = "slate.httpdemo"
    }
}
