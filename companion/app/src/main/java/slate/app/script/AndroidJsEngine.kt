package slate.app.script

import android.annotation.SuppressLint
import android.content.Context
import slate.app.link.LinkLog
import androidx.javascriptengine.IsolateStartupParameters
import androidx.javascriptengine.JavaScriptIsolate
import androidx.javascriptengine.JavaScriptSandbox
import com.google.common.util.concurrent.ListenableFuture
import com.google.common.util.concurrent.MoreExecutors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
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
                dropSharedSandboxAsync()
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
         * IllegalStateException("Binding to already bound service"). The
         * per-sub-app boundary is the isolate; the sandbox is the shared V8 host.
         *
         * [sandboxInstance] is the live handle that must be closed to reopen
         * androidx's static gate. Clearing [sandboxJob] alone without close()
         * bricks every later launch ("bound but unreachable") until force-stop —
         * that is what the operator hit after OTA reconnects (N-51 class).
         */
        private val sandboxLock = Mutex()
        private val sandboxScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        @Volatile
        private var sandboxJob: Deferred<JavaScriptSandbox>? = null

        /** Last successfully bound sandbox; closed before any rebind. */
        @Volatile
        private var sandboxInstance: JavaScriptSandbox? = null

        private suspend fun sharedSandbox(context: Context): JavaScriptSandbox {
            sandboxInstance?.let { return it }

            val deferred = sandboxLock.withLock {
                sandboxInstance?.let { return@withLock null }
                sandboxJob ?: newSandboxAsync(context).also { sandboxJob = it }
            }
            if (deferred == null) {
                return sandboxInstance
                    ?: throw ScriptEngineException(
                        "JS sandbox instance missing after bind race",
                    )
            }

            return try {
                deferred.await()
            } catch (t: Throwable) {
                sandboxLock.withLock {
                    if (sandboxJob === deferred) sandboxJob = null
                }
                if (isAlreadyBound(t)) {
                    // Gate shut, our Deferred failed — try to close any handle
                    // we still hold, then one clean rebind.
                    LinkLog.w("JS sandbox already bound (${t.message}); forcing release + retry")
                    releaseSharedSandbox()
                    delay(100)
                    return bindFresh(context)
                }
                throw t
            }
        }

        private suspend fun bindFresh(context: Context): JavaScriptSandbox {
            val deferred = sandboxLock.withLock {
                sandboxInstance?.let { return@withLock null }
                newSandboxAsync(context).also { sandboxJob = it }
            }
            if (deferred == null) {
                return sandboxInstance
                    ?: throw ScriptEngineException(
                        "JS sandbox is bound but unreachable — this process cannot " +
                            "start another. Force-stop Slate and reopen it.",
                    )
            }
            return try {
                deferred.await()
            } catch (t: Throwable) {
                sandboxLock.withLock {
                    if (sandboxJob === deferred) sandboxJob = null
                }
                if (isAlreadyBound(t)) {
                    throw ScriptEngineException(
                        "JS sandbox is bound but unreachable — this process cannot " +
                            "start another. Force-stop Slate and reopen it.",
                    )
                }
                throw t
            }
        }

        private fun isAlreadyBound(t: Throwable): Boolean =
            t.message?.contains("already bound", ignoreCase = true) == true ||
                t.cause?.message?.contains("already bound", ignoreCase = true) == true

        @SuppressLint("RequiresFeature") // Guarded explicitly; lint cannot infer fail-closed branch.
        private fun newSandboxAsync(context: Context): Deferred<JavaScriptSandbox> =
            sandboxScope.async {
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
                sandboxLock.withLock {
                    sandboxInstance = sandbox
                }
                sandbox
            }

        suspend fun create(context: Context): AndroidJsEngine = withContext(Dispatchers.IO) {
            try {
                createOnce(context)
            } catch (t: Throwable) {
                if (!looksLikeSandboxBrick(t)) throw wrapUnavailable(t)
                LinkLog.w("JS create brick (${t.message}); forceReset + retry")
                releaseSharedSandbox()
                delay(150)
                try {
                    createOnce(context)
                } catch (t2: Throwable) {
                    throw wrapUnavailable(t2)
                }
            }
        }

        private fun wrapUnavailable(t: Throwable): ScriptEngineException =
            if (t is ScriptEngineException && t.message?.startsWith("JS sandbox") == true) {
                t
            } else {
                ScriptEngineException("JS sandbox unavailable: ${t.message}", t)
            }

        private fun looksLikeSandboxBrick(t: Throwable): Boolean {
            var cur: Throwable? = t
            while (cur != null) {
                val m = cur.message.orEmpty()
                if (m.contains("already bound", ignoreCase = true) ||
                    m.contains("bound but unreachable", ignoreCase = true) ||
                    m.contains("sandbox unavailable", ignoreCase = true)
                ) {
                    return true
                }
                cur = cur.cause
            }
            return false
        }

        private suspend fun createOnce(context: Context): AndroidJsEngine {
            val params = IsolateStartupParameters()
            params.maxHeapSizeBytes = HEAP_BYTES
            var sandbox = try {
                sharedSandbox(context)
            } catch (t: Throwable) {
                throw ScriptEngineException("JS sandbox unavailable: ${t.message}", t)
            }
            val isolate = try {
                sandbox.createIsolate(params)
            } catch (t: IllegalStateException) {
                // Host process reclaimed under memory pressure — dead handle.
                LinkLog.w("JS sandbox handle stale (${t.message}); rebinding")
                releaseSharedSandbox()
                try {
                    sandbox = sharedSandbox(context)
                    sandbox.createIsolate(params)
                } catch (t2: Throwable) {
                    throw ScriptEngineException(
                        "JS sandbox could not be rebound: ${t2.message}",
                        t2,
                    )
                }
            }
            return AndroidJsEngine(
                isolate,
                canTerminateIsolate = sandbox.isFeatureSupported(
                    JavaScriptSandbox.JS_FEATURE_ISOLATE_TERMINATION,
                ),
            )
        }

        /**
         * Tear down every isolate's host process and clear our statics.
         *
         * Call after a launch failure that smells like N-51, or when the link
         * service is destroyed, so the next sub-app can bind again without a
         * force-stop.
         */
        suspend fun forceReset() {
            LinkLog.i("JS sandbox forceReset")
            releaseSharedSandbox()
        }

        /**
         * Unbind and forget the shared sandbox.
         *
         * `close()` is what actually releases the service binding; clearing the
         * job alone leaves androidx believing it is still bound.
         */
        private suspend fun releaseSharedSandbox() {
            val (job, instance) = sandboxLock.withLock {
                val j = sandboxJob
                val i = sandboxInstance
                sandboxJob = null
                sandboxInstance = null
                j to i
            }
            if (instance != null) {
                runCatching { instance.close() }
                    .onFailure { e -> LinkLog.i("JS sandbox instance close: ${e.message}") }
                // Job may still be the same object; do not close twice.
                return
            }
            if (job == null) return
            runCatching { job.await() }
                .onSuccess {
                    runCatching { it.close() }
                        .onFailure { e -> LinkLog.i("JS sandbox job close: ${e.message}") }
                }
        }

        /**
         * Emergency teardown from the non-suspending evaluate() path.
         * Never null the statics without scheduling a close — that is the
         * brick that forced force-stop after OTA.
         */
        private fun dropSharedSandboxAsync() {
            sandboxScope.launch {
                releaseSharedSandbox()
            }
        }
    }
}

/**
 * Await a ListenableFuture — **without** propagating cancellation to it.
 *
 * androidx's bind future unbinds on cancellation but does not reset its static
 * `sIsReadyToConnect` gate, so cancelling a sandbox bind leaves the process
 * permanently unable to create another one. Letting the bind run to completion
 * means we hold a reference that can be closed. Do not add
 * `invokeOnCancellation { cancel() }` here.
 */
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
