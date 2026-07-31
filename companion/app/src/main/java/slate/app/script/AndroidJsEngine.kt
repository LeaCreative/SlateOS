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
    private val sandbox: JavaScriptSandbox,
    private val isolate: JavaScriptIsolate,
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
            // Closing a sandbox terminates its isolated process even when the
            // WebView implementation lacks per-isolate termination support.
            sandbox.close()
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
        try {
            isolate.close()
        } finally {
            sandbox.close()
        }
    }

    companion object {
        /** 4 MB heap per isolate (§6.5). */
        private const val HEAP_BYTES = 4L * 1024L * 1024L
        /** Last-resort kill; fine-grained governor limits remain lower. */
        private const val EVAL_TIMEOUT_MS = 500L

        @SuppressLint("RequiresFeature") // Guarded explicitly; lint cannot infer fail-closed branch.
        suspend fun create(context: Context): AndroidJsEngine = withContext(Dispatchers.IO) {
            val sandbox = JavaScriptSandbox.createConnectedInstanceAsync(context.applicationContext).awaitFuture()
            if (!sandbox.isFeatureSupported(
                    JavaScriptSandbox.JS_FEATURE_ISOLATE_MAX_HEAP_SIZE,
                )
            ) {
                sandbox.close()
                throw ScriptEngineException(
                    "WebView JavaScript sandbox cannot enforce the 4 MB isolate heap limit",
                )
            }
            val params = IsolateStartupParameters()
            params.maxHeapSizeBytes = HEAP_BYTES
            val isolate = sandbox.createIsolate(params)
            AndroidJsEngine(sandbox, isolate)
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
