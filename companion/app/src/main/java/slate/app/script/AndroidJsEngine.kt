package slate.app.script

import android.annotation.SuppressLint
import android.content.Context
import androidx.javascriptengine.IsolateStartupParameters
import androidx.javascriptengine.JavaScriptIsolate
import androidx.javascriptengine.JavaScriptSandbox
import com.google.common.util.concurrent.ListenableFuture
import com.google.common.util.concurrent.MoreExecutors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import slate.script.ScriptEngine
import slate.script.ScriptEngineException
import java.util.concurrent.TimeUnit
import java.util.concurrent.TimeoutException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/**
 * One [JavaScriptIsolate] per sub-app (§6.4).
 * V8 runs in an isolated process exclusive to this app — sandbox escape
 * costs that process, not the companion UI / BLE service heap.
 */
class AndroidJsEngine private constructor(
    private val isolate: JavaScriptIsolate,
    /** Whether this WebView can kill one isolate without dropping the sandbox. */
    private val canTerminateIsolate: Boolean,
) : ScriptEngine {

    override var lastEvalMs: Long = 0L
        private set

    override fun evaluate(source: String): String {
        val t0 = System.nanoTime()
        val future = isolate.evaluateJavaScriptAsync(source)
        return try {
            future.get(EVAL_TIMEOUT_MS, TimeUnit.MILLISECONDS) ?: ""
        } catch (t: TimeoutException) {
            future.cancel(true)
            isolate.close()
            if (!canTerminateIsolate) {
                // This WebView cannot terminate a single isolate, so the only
                // way to stop a script that has blown the hard deadline is to
                // drop the whole sandbox process — which takes every other
                // sub-app's isolate with it. Accepted deliberately: the 500 ms
                // deadline is a safety property, and a runaway script must not
                // be able to outlive it. The next create() reconnects.
                dropSharedSandbox()
            }
            throw ScriptEngineException(
                "JS isolate exceeded the ${EVAL_TIMEOUT_MS}ms hard deadline",
                t,
            )
        } catch (t: Throwable) {
            throw ScriptEngineException("JS isolate eval failed: ${t.message}", t)
        } finally {
            lastEvalMs = (System.nanoTime() - t0) / 1_000_000L
        }
    }

    override fun close() {
        // Only the isolate. The sandbox is shared process-wide (see create) —
        // closing it here would tear down the connection out from under every
        // other running sub-app.
        isolate.close()
    }

    companion object {
        /** 4 MB heap per isolate (§6.5). */
        private const val HEAP_BYTES = 4L * 1024L * 1024L
        /** Last-resort kill; fine-grained governor limits remain lower. */
        private const val EVAL_TIMEOUT_MS = 500L

        /**
         * The sandbox is per PROCESS, not per sub-app.
         *
         * androidx.javascriptengine permits exactly one bound JavaScriptSandbox
         * per process; a second createConnectedInstanceAsync() throws
         * IllegalStateException("Binding to already bound service") on the main
         * thread and takes the whole app down with it. That is what happened
         * every time a second JS sub-app was opened, or the foreground service
         * sticky-restarted while the first binding was still alive: the
         * companion died, the Activity came back at its main menu, and the
         * restarted service briefly raced a second GATT connection against the
         * first (duplicate onServicesDiscovered, writeDescriptor rc=201).
         *
         * The per-sub-app boundary is unaffected: each still gets its own
         * JavaScriptIsolate with its own 4 MB heap, which is the actual
         * sandboxing unit. The sandbox is just the shared V8 host process.
         */
        private val sandboxLock = Mutex()

        @Volatile
        private var shared: JavaScriptSandbox? = null

        @SuppressLint("RequiresFeature") // Guarded explicitly; lint cannot infer fail-closed branch.
        private suspend fun sharedSandbox(context: Context): JavaScriptSandbox =
            sandboxLock.withLock {
                shared?.let { return@withLock it }
                val sandbox = JavaScriptSandbox
                    .createConnectedInstanceAsync(context.applicationContext)
                    .awaitFuture()
                if (!sandbox.isFeatureSupported(
                        JavaScriptSandbox.JS_FEATURE_ISOLATE_MAX_HEAP_SIZE,
                    )
                ) {
                    sandbox.close()
                    throw ScriptEngineException(
                        "WebView JavaScript sandbox cannot enforce the 4 MB isolate heap limit",
                    )
                }
                shared = sandbox
                sandbox
            }

        suspend fun create(context: Context): AndroidJsEngine = withContext(Dispatchers.IO) {
            val params = IsolateStartupParameters()
            params.maxHeapSizeBytes = HEAP_BYTES
            val isolate = try {
                sharedSandbox(context).createIsolate(params)
            } catch (t: IllegalStateException) {
                // The host process can be reclaimed under memory pressure,
                // leaving a dead handle. Drop it and reconnect once rather than
                // failing every sub-app launch until the app is restarted.
                sandboxLock.withLock { shared = null }
                sharedSandbox(context).createIsolate(params)
            }
            AndroidJsEngine(
                isolate,
                canTerminateIsolate = sharedSandbox(context).isFeatureSupported(
                    JavaScriptSandbox.JS_FEATURE_ISOLATE_TERMINATION,
                ),
            )
        }

        /**
         * Emergency teardown for a script that ignored the eval deadline.
         *
         * Not taken under [sandboxLock] — this runs from the non-suspending
         * evaluate() path, and a runaway isolate must not wait on a lock a
         * create() may be holding. A create() racing this reconnects, which is
         * the correct outcome either way.
         */
        private fun dropSharedSandbox() {
            val doomed = shared
            shared = null
            try {
                doomed?.close()
            } catch (_: Throwable) {
                // Already dead; nothing to salvage.
            }
        }
    }
}

private suspend fun <T> ListenableFuture<T>.awaitFuture(): T =
    suspendCancellableCoroutine { cont ->
        addListener(
            {
                try {
                    cont.resume(get())
                } catch (t: Throwable) {
                    cont.resumeWithException(t.cause ?: t)
                }
            },
            MoreExecutors.directExecutor(),
        )
    }
