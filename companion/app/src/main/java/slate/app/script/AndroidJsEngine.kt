package slate.app.script

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
        return try {
            isolate.evaluateJavaScriptAsync(source).get() ?: ""
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

        suspend fun create(context: Context): AndroidJsEngine = withContext(Dispatchers.IO) {
            val sandbox = JavaScriptSandbox.createConnectedInstanceAsync(context.applicationContext).awaitFuture()
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
