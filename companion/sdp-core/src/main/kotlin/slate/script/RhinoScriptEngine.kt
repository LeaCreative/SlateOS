package slate.script

import org.mozilla.javascript.Context
import org.mozilla.javascript.ScriptableObject

/**
 * In-process Rhino engine for desktop emulator + JVM unit tests.
 * Binding surface matches the Android isolate path (string in / string out).
 *
 * Context is entered per [evaluate] (Rhino is thread-local); a lock serializes
 * access so Dispatchers.Default is safe.
 */
class RhinoScriptEngine : ScriptEngine {
    private val lock = Any()
    private var scope: ScriptableObject? = null
    override var lastEvalMs: Long = 0L
        private set

    override fun evaluate(source: String): String = synchronized(lock) {
        val t0 = System.nanoTime()
        val cx = Context.enter()
        return try {
            cx.optimizationLevel = -1
            cx.languageVersion = Context.VERSION_ES6
            val sc = scope ?: cx.initStandardObjects().also { scope = it }
            val result = cx.evaluateString(sc, source, "slate", 1, null)
            Context.toString(result) ?: ""
        } catch (t: Throwable) {
            throw ScriptEngineException("Rhino eval failed: ${t.message}", t)
        } finally {
            Context.exit()
            lastEvalMs = (System.nanoTime() - t0) / 1_000_000L
        }
    }

    override fun close() {
        synchronized(lock) {
            scope = null
        }
    }
}
