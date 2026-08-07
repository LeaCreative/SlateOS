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
import kotlinx.coroutines.launch
import kotlinx.coroutines.async
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.CoroutineScope
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

        /**
         * Creation runs here, **not** in the caller's scope.
         *
         * androidx gates sandbox creation on a private static
         * `sIsReadyToConnect`, and the library's own documentation is explicit
         * that only `close()` on the sandbox object resets it — there is no
         * static recovery. So a sandbox reference that is ever dropped without
         * being closed bricks JS for the rest of the process's life.
         *
         * Creating it in the caller's coroutine made exactly that reachable:
         * `awaitFuture` suspends on a `suspendCancellableCoroutine`, and if the
         * caller was cancelled after the bind completed, the future's listener
         * resumed a dead continuation, the object was garbage and the static
         * stayed false. Every later launch then failed with "Binding to already
         * bound service" from the *first* call, with no reference left to close.
         *
         * Owning the work here means a cancelled caller cancels only its own
         * `await()`. The [Deferred] keeps the reference either way.
         */
        private val sandboxScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

        @Volatile
        private var sandboxJob: Deferred<JavaScriptSandbox>? = null

        private suspend fun sharedSandbox(context: Context): JavaScriptSandbox {
            val deferred = sandboxLock.withLock {
                sandboxJob ?: newSandboxAsync(context).also { sandboxJob = it }
            }
            return try {
                deferred.await()
            } catch (t: Throwable) {
                // Creation failed and nothing is bound (every failing path in
                // newSandboxAsync closes or never bound). Clear so the next
                // launch can try again rather than caching the failure forever.
                sandboxLock.withLock { if (sandboxJob === deferred) sandboxJob = null }
                if (t.message?.contains("already bound") == true) {
                    throw ScriptEngineException(
                        "JS sandbox is bound but unreachable — this process cannot " +
                            "start another. Force-stop Slate and reopen it.",
                    )
                }
                throw t
            }
        }

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
                    // close() is what resets androidx's static gate. Throwing
                    // without it would brick JS for the process.
                    sandbox.close()
                    throw ScriptEngineException(
                        "WebView JavaScript sandbox cannot enforce the 4 MB isolate heap limit",
                    )
                }
                sandbox
            }

        suspend fun create(context: Context): AndroidJsEngine = withContext(Dispatchers.IO) {
            val params = IsolateStartupParameters()
            params.maxHeapSizeBytes = HEAP_BYTES
            val sandbox = try {
                sharedSandbox(context)
            } catch (t: Throwable) {
                throw ScriptEngineException("JS sandbox unavailable: ${t.message}")
            }
            val isolate = try {
                sandbox.createIsolate(params)
            } catch (t: IllegalStateException) {
                // The host process can be reclaimed under memory pressure,
                // leaving a dead handle. Reconnect once rather than failing
                // every sub-app launch until the app is restarted.
                //
                // CLOSING the old sandbox is the whole point and used to be
                // missing. Dropping the reference does not unbind the service,
                // so the rebind below hit
                // IllegalStateException("Binding to already bound service") —
                // thrown from a coroutine with no handler, which killed the
                // whole companion. The recovery path could not succeed; it only
                // turned a recoverable fault into process death, and it was
                // reached most often by the sub-app opened when memory was
                // tightest. (Crash log 7 Aug, opening Local Map.)
                LinkLog.w("JS sandbox handle stale (${t.message}); rebinding")
                releaseSharedSandbox()
                try {
                    sharedSandbox(context).createIsolate(params)
                } catch (t2: Throwable) {
                    // Fail this sub-app, not the process. The BLE link is worth
                    // more than any one screen.
                    throw ScriptEngineException(
                        "JS sandbox could not be rebound: ${t2.message}",
                    )
                }
            }
            AndroidJsEngine(
                isolate,
                canTerminateIsolate = sandbox.isFeatureSupported(
                    JavaScriptSandbox.JS_FEATURE_ISOLATE_TERMINATION,
                ),
            )
        }

        /**
         * Unbind and forget the shared sandbox.
         *
         * `close()` is what actually releases the service binding; clearing the
         * reference alone leaves androidx believing it is still bound and makes
         * the next bind throw. Failures here are swallowed deliberately — this
         * runs while recovering from an already-broken sandbox, and the handle
         * being dead is the normal case rather than an error.
         */
        private suspend fun releaseSharedSandbox() {
            val old = sandboxLock.withLock {
                val d = sandboxJob
                sandboxJob = null
                d
            }
            if (old == null) return
            // await() returns immediately for a settled Deferred and rethrows a
            // failed one; either way the point is to reach close(), because that
            // is the only thing that lets this process bind again.
            runCatching { old.await() }
                .onSuccess {
                    runCatching { it.close() }
                        .onFailure { e -> LinkLog.i("old JS sandbox close: ${e.message}") }
                }
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
            val doomed = sandboxJob
            sandboxJob = null
            if (doomed == null) return
            // Reached from the non-suspending evaluate() path, so the sandbox is
            // long since created and getCompleted() is safe. Closing is the
            // whole point — skipping it would leave androidx's static gate shut
            // with no reference left to open it.
            if (doomed.isCompleted) {
                runCatching { doomed.getCompleted().close() }
                return
            }
            // Still connecting, which should not happen here. Close from the
            // owning scope rather than dropping the reference on the floor.
            sandboxScope.launch {
                runCatching { doomed.await() }.onSuccess { runCatching { it.close() } }
            }
        }
    }
}

/**
 * Await a ListenableFuture — **without** propagating cancellation to it.
 *
 * That looks like an omission and is not. androidx's bind future unbinds on
 * cancellation but does not reset its static `sIsReadyToConnect` gate, so
 * cancelling a sandbox bind leaves the process permanently unable to create
 * another one. Letting the bind run to completion means the [Deferred] in
 * [AndroidJsEngine] holds a reference that can be closed, which is the only
 * thing that reopens the gate. Do not add `invokeOnCancellation { cancel() }`
 * here.
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
