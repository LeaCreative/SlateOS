package slate.app.script

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import slate.compositor.Compositor
import slate.host.HostInbound
import slate.script.JsSlateAppEndpoint
import slate.script.ScriptConsole
import slate.script.ScriptPermission
import slate.script.ScriptResources

/**
 * Owns JS sub-app sessions: one isolate each, timer fan-out, IPC latency probe.
 */
class ScriptRuntimeHost(
    private val context: Context,
    private val scope: CoroutineScope,
    private val compositor: Compositor,
) {
    private var timerApp: JsSlateAppEndpoint? = null
    private val timerJobs = HashMap<String, Job>()
    var lastRenderIpcMs: Long = -1L
        private set

    suspend fun ensureTimerRegistered() {
        if (timerApp != null) return
        val engine = AndroidJsEngine.create(context)
        // Measure isolate IPC before loading app logic (§6.4).
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

        val ep = JsSlateAppEndpoint.loadTimer(
            engine = engine,
            hostHeld = setOf(ScriptPermission.Storage),
            onTimerSet = { id, ms -> scheduleTimer(id, ms) },
            onTimerClear = { id -> clearTimer(id) },
        )
        // ui+host already loaded; install app entry only
        engine.evaluate(ScriptResources.read(ScriptResources.TIMER_MAIN))
        compositor.register(ep)
        timerApp = ep
    }

    private fun scheduleTimer(id: String, intervalMs: Long) {
        clearTimer(id)
        val appId = timerApp?.manifest?.id ?: return
        timerJobs[id] = scope.launch {
            while (isActive) {
                delay(intervalMs)
                val payload = JSONObject().put("id", id).toString()
                compositor.dispatchSystemEvent(appId, "timer", payload)
            }
        }
    }

    private fun clearTimer(id: String) {
        timerJobs.remove(id)?.cancel()
    }

    fun close() {
        timerJobs.values.forEach { it.cancel() }
        timerJobs.clear()
        timerApp?.close()
        timerApp = null
    }
}
