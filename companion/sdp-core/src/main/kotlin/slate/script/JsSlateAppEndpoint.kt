package slate.script

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import slate.host.AppManifest
import slate.host.HostInbound
import slate.host.HostOutbound
import slate.host.PriorityClass
import slate.host.RefreshPolicy
import slate.host.SlateAppEndpoint

/**
 * [SlateAppEndpoint] backed by a JS [ScriptEngine].
 *
 * **Isolate lifecycle: one engine / isolate per running sub-app.**
 *
 * Justification:
 * - Independent 4 MB heap accounting (§6.5) without cross-app contamination
 * - Clean kill on governor overrun (close isolate = reclaim process heap slice)
 * - No shared V8/Rhino mutable state between untrusted scripts
 * Isolate create cost is amortized over a session; sharing couples failure domains.
 *
 * Process-boundary: JSON in / JSON out (+ base64 display lists). Android uses
 * androidx.javascriptengine IPC; desktop/tests use Rhino with the same surface.
 */
class JsSlateAppEndpoint(
    val scriptManifest: ScriptManifest,
    private val engine: ScriptEngine,
    private val governor: Governor = Governor(),
    private val nowMs: () -> Long = { System.currentTimeMillis() },
    hostHeldPermissions: Set<ScriptPermission> = ScriptPermission.entries.toSet(),
    private val onTimerSet: (id: String, intervalMs: Long) -> Unit = { _, _ -> },
    private val onTimerClear: (id: String) -> Unit = { _ -> },
) : SlateAppEndpoint, AutoCloseable {

    override val manifest: AppManifest = manifestFrom(scriptManifest)

    val gov: Governor get() = governor

    private val store = LinkedHashMap<String, String>()
    private val bindings = BindingSurface(
        script = scriptManifest,
        governor = governor,
        hostHeld = hostHeldPermissions,
        store = store,
        nowMs = nowMs,
        onTimerSet = onTimerSet,
        onTimerClear = onTimerClear,
        appId = scriptManifest.id,
    )

    suspend fun installRuntime(
        uiJs: String = ScriptResources.read(ScriptResources.UI_JS),
        hostJs: String = ScriptResources.read(ScriptResources.HOST_JS),
        appJs: String,
    ) {
        withContext(Dispatchers.Default) {
            engine.evaluate(uiJs)
            engine.evaluate(hostJs)
            engine.evaluate(appJs)
        }
    }

    override suspend fun dispatch(msg: HostInbound): List<HostOutbound> {
        if (governor.isDisabled) {
            ScriptConsole.log(scriptManifest.id, "error", "disabled: ${governor.reason}")
            return listOf(HostOutbound.Log("error", "disabled: ${governor.reason}"))
        }
        val kind = when (msg) {
            HostInbound.Render, HostInbound.Focus -> Governor.Kind.RenderTimeout
            is HostInbound.Input, is HostInbound.SystemEvent -> Governor.Kind.EventTimeout
            else -> null
        }
        val json = HostJson.encodeInbound(msg)
        val raw = withContext(Dispatchers.Default) {
            try {
                engine.evaluate("var __slate_store = ${bindings.injectStoreJson()};")
                engine.evaluate("__slate_dispatch(" + JSONObjectQuote.quote(json) + ")")
            } catch (t: ScriptEngineException) {
                ScriptConsole.log(scriptManifest.id, "error", t.message ?: "eval")
                return@withContext "[]"
            }
        }
        val elapsed = engine.lastEvalMs
        ScriptConsole.timing(scriptManifest.id, msg::class.simpleName ?: "msg", elapsed, nowMs())
        if (kind != null && !governor.checkDuration(kind, elapsed, nowMs())) {
            val v = governor.violationLog.lastOrNull()
            if (v != null) {
                ScriptConsole.violation(
                    scriptManifest.id,
                    "${v.kind}: ${v.detail}" + if (governor.isDisabled) " (DISABLED)" else " (killed call)",
                )
            }
            return listOf(HostOutbound.Log("error", "killed: $kind ${elapsed}ms"))
        }
        val decoded = HostJson.decodeOutboundList(raw)
        decoded.filterIsInstance<HostOutbound.Log>().forEach {
            ScriptConsole.log(scriptManifest.id, it.level, it.message)
        }
        return bindings.filterOutbound(decoded)
    }

    /** Wall-clock probe of a no-op render round-trip (for Gate F / §6.4). */
    suspend fun measureRenderRoundTripMs(samples: Int = 5): Long {
        installEmptyRenderIfNeeded()
        var total = 0L
        repeat(samples) {
            dispatch(HostInbound.Render)
            total += engine.lastEvalMs
        }
        return total / samples
    }

    private var emptyInstalled = false
    private suspend fun installEmptyRenderIfNeeded() {
        if (emptyInstalled) return
        withContext(Dispatchers.Default) {
            engine.evaluate("function render(){ return []; }")
        }
        emptyInstalled = true
    }

    override fun close() {
        engine.close()
    }

    companion object {
        fun manifestFrom(script: ScriptManifest): AppManifest {
            val refresh = when {
                script.refreshPolicy.equals("periodic", true) &&
                    script.refreshIntervalMs >= 1000L ->
                    RefreshPolicy.Periodic(script.refreshIntervalMs)
                script.refreshPolicy.equals("manual", true) -> RefreshPolicy.Manual
                else -> RefreshPolicy.OnChange
            }
            return AppManifest(
                id = script.id,
                name = script.name,
                version = script.version,
                minProtocolVersion = script.minProtocolVersion,
                minHostVersion = script.minHostVersion,
                defaultPriority = PriorityClass.fromManifest(script.priority),
                refresh = refresh,
            )
        }

        fun loadTimer(
            engine: ScriptEngine,
            hostHeld: Set<ScriptPermission> = setOf(ScriptPermission.Storage),
            onTimerSet: (String, Long) -> Unit = { _, _ -> },
            onTimerClear: (String) -> Unit = {},
        ): JsSlateAppEndpoint {
            val script = ScriptResources.parseManifest(
                ScriptResources.read(ScriptResources.TIMER_MANIFEST),
            )
            return JsSlateAppEndpoint(
                scriptManifest = script,
                engine = engine,
                hostHeldPermissions = hostHeld,
                onTimerSet = onTimerSet,
                onTimerClear = onTimerClear,
            )
        }
    }
}

/** Minimal JSON string quoting. */
internal object JSONObjectQuote {
    fun quote(s: String): String {
        val sb = StringBuilder(s.length + 2)
        sb.append('"')
        for (c in s) {
            when (c) {
                '\\' -> sb.append("\\\\")
                '"' -> sb.append("\\\"")
                '\n' -> sb.append("\\n")
                '\r' -> sb.append("\\r")
                '\t' -> sb.append("\\t")
                else -> if (c.code < 0x20) {
                    sb.append("\\u%04x".format(c.code))
                } else {
                    sb.append(c)
                }
            }
        }
        sb.append('"')
        return sb.toString()
    }
}
