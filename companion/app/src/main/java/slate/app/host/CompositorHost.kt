package slate.app.host

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import slate.app.apps.ClockApp
import slate.app.apps.TestApp
import slate.app.link.LinkLog
import slate.app.link.SharedLink
import slate.app.link.SlateGattClient
import slate.compositor.Compositor
import slate.compositor.FocusReason
import slate.compositor.StackOp
import slate.host.PriorityClass

/**
 * Binds [Compositor] to the GATT link inside the FGS.
 */
class CompositorHost(
    private val gatt: SlateGattClient,
    private val scope: CoroutineScope,
) {
    val compositor = Compositor(
        nowMs = { System.currentTimeMillis() },
        pushToWatch = { bytes ->
            if (SharedLink.benchmarkPaused) return@Compositor false
            gatt.pushDisplayList(bytes)
        },
    )

    private var tickJob: Job? = null
    private val clock = ClockApp()
    private val test = TestApp()

    fun start() {
        compositor.register(clock)
        compositor.register(test)
        scope.launch {
            gatt.metrics.collect { m ->
                compositor.linkConnected = m.connected
                if (m.connected) {
                    // Until HELLO_OFFER/ACCEPT is parsed on the phone, assume protocol 1
                    // and a full DL buffer (matches firmware kMaxListBytes).
                    if (compositor.watchProtocolVersion < 1) {
                        compositor.watchProtocolVersion = 1
                    }
                    if (compositor.freeCreditBytes <= 0) {
                        compositor.setCredit(4096)
                    }
                    ensureAmbient()
                    startTicker()
                } else {
                    stopTicker()
                }
            }
        }
    }

    fun stop() {
        stopTicker()
    }

    fun setWatchProtocolVersion(version: Int) {
        compositor.watchProtocolVersion = version
    }

    fun setCredit(freeBytes: Int) {
        compositor.setCredit(freeBytes)
    }

    suspend fun openTestApp() {
        compositor.requestFocus(
            test.manifest.id,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
    }

    private suspend fun ensureAmbient() {
        if (compositor.stackSnapshot.any { it.appId == clock.manifest.id }) return
        val deny = compositor.requestFocus(
            clock.manifest.id,
            PriorityClass.AMBIENT,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
        if (deny != null) {
            LinkLog.w("ambient clock focus denied: $deny")
        }
    }

    private fun startTicker() {
        if (tickJob?.isActive == true) return
        tickJob = scope.launch {
            while (isActive) {
                if (!SharedLink.benchmarkPaused) {
                    compositor.tick()
                }
                delay(TICK_MS)
            }
        }
    }

    private fun stopTicker() {
        tickJob?.cancel()
        tickJob = null
    }

    companion object {
        const val TICK_MS = 500L
    }
}
