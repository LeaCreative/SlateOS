package slate.script

/**
 * Engine-agnostic JS evaluation (process-boundary ready).
 * Android: androidx.javascriptengine isolate.
 * Desktop/tests: Rhino in-process (same binding surface).
 */
interface ScriptEngine : AutoCloseable {
    /** Evaluate [source]; return string result (may be empty). */
    fun evaluate(source: String): String

    /** Wall-clock ms of the last [evaluate] call (for Gate F / governor). */
    val lastEvalMs: Long
}

class ScriptEngineException(message: String, cause: Throwable? = null) :
    RuntimeException(message, cause)
